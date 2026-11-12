# PlatformIO post-build script: copy firmware.bin to firmware_out/ with the
# version in the name, ready to send for the browser upload page
# (http://<board ip>/update). Example: firmware_out/TV_IR_IN_ONE_v1.1.0.bin
Import("env")
import os, re, shutil

def export_bin(source, target, env):
    proj = env.subst("$PROJECT_DIR")
    ver = "unknown"
    with open(os.path.join(proj, "src", "Config.h"), encoding="utf-8") as f:
        m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', f.read())
        if m:
            ver = m.group(1)
    out_dir = os.path.join(proj, "firmware_out")
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, "TV_IR_IN_ONE_v%s.bin" % ver)
    shutil.copyfile(str(target[0]), dst)
    print("Firmware for browser upload: %s" % dst)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_bin)
