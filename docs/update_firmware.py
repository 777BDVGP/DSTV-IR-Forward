# Copies the newest "full" firmware of each project into docs/firmware/ and
# writes the manifests for the Install page (docs/index.html, ESP Web Tools).
#
#   1. Build the three projects (each build writes firmware_out/*_full.bin)
#   2. python docs/update_firmware.py
#   3. Commit docs/ and push - GitHub Pages then serves the new versions
#
# Only files built WITHOUT src/Private.h are taken: a *_full_PRIVATE.bin holds
# your own password / WiFi / access point names and is never published.
import glob, json, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
BOARDS = [  # project folder, file + manifest name, title on the installer
    ("TV_IR_Sending",   "sender",   "IR Forward Sender"),
    ("TV_IR_Receiving", "receiver", "IR Forward Receiver"),
    ("TV_IR_IN_ONE",    "allinone", "IR Forward All-in-One"),
]

def vkey(path):
    m = re.search(r"_v([\d.]+)_full\.bin$", path)
    return [int(x) for x in m.group(1).split(".")] if m else []

os.makedirs(os.path.join(DOCS, "firmware"), exist_ok=True)
ok = True
for proj, short, title in BOARDS:
    if os.path.exists(os.path.join(ROOT, proj, "src", "Private.h")):
        print("%s: src/Private.h exists - build in a copy without it (public copy)" % proj)
        ok = False
        continue
    files = [f for f in glob.glob(os.path.join(ROOT, proj, "firmware_out", "*_full.bin"))]
    if not files:
        print("%s: no *_full.bin in firmware_out - build the project first" % proj)
        ok = False
        continue
    newest = max(files, key=vkey)
    version = re.search(r"_v([\d.]+)_full\.bin$", newest).group(1)
    shutil.copyfile(newest, os.path.join(DOCS, "firmware", short + ".bin"))
    manifest = {
        "name": title,
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S2",
                    "parts": [{"path": "firmware/%s.bin" % short, "offset": 0}]}],
    }
    with open(os.path.join(DOCS, "manifest_%s.json" % short), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print("%-16s v%-6s -> docs/firmware/%s.bin" % (proj, version, short))
sys.exit(0 if ok else 1)
