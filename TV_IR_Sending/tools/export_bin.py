# PlatformIO post-build script (same file in TV_IR_Sending, TV_IR_Receiving
# and TV_IR_IN_ONE). After every build it writes to firmware_out/:
#
#   <project>_v<ver>.bin       the program only - for updates over WiFi
#                              (All-in-One: http://<board ip>/update)
#   <project>_v<ver>_full.bin  bootloader + partition table + program in ONE
#                              file, for the FIRST flash of a new S2 Mini at
#                              address 0x0 - with a browser, no PlatformIO
#                              (see README "Flashing without PlatformIO")
#
# A build with src/Private.h holds your own password / WiFi / AP names: its
# full file is then named ..._full_PRIVATE.bin - never give that one away.
# The file system is not included: the firmware formats it on the first boot.
Import("env")
import os, re, shutil, subprocess

def fw_version(proj):
    for name in ("Config.h", "01_Main.h"):
        p = os.path.join(proj, "src", name)
        if os.path.exists(p):
            m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', open(p, encoding="utf-8").read())
            if m:
                return m.group(1)
    return "unknown"

def export_bin(source, target, env):
    proj = env.subst("$PROJECT_DIR")
    name = os.path.basename(proj)
    ver  = fw_version(proj)
    out_dir = os.path.join(proj, "firmware_out")
    os.makedirs(out_dir, exist_ok=True)

    app = str(target[0])
    upd = os.path.join(out_dir, "%s_v%s.bin" % (name, ver))
    shutil.copyfile(app, upd)
    print("Firmware for updates:   %s" % upd)

    private = os.path.exists(os.path.join(proj, "src", "Private.h"))
    full = os.path.join(out_dir, "%s_v%s_full%s.bin" % (name, ver, "_PRIVATE" if private else ""))
    parts = []
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):       # bootloader, partitions, boot_app0
        parts += [env.subst(offset), env.subst(image)]
    parts += [env.subst("$ESP32_APP_OFFSET"), app]
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    cmd = [env.subst("$PYTHONEXE"), esptool, "--chip", env.BoardConfig().get("build.mcu", "esp32s2"),
           "merge_bin", "-o", full,
           "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"] + parts
    if subprocess.call(cmd) == 0:
        print("Firmware for a NEW board (browser flash at 0x0): %s" % full)
        if private:
            print("  NOTE: built with src/Private.h - your own settings are inside, do not share it")
    else:
        print("WARNING: could not create %s" % full)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_bin)
