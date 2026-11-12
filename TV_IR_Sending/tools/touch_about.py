# PlatformIO pre-build script: touch the About source so __DATE__/__TIME__
# (Build Date on the About page) is the time of THIS build, not of the last
# change to that file.
Import("env")
import os
path = os.path.join(env.subst("$PROJECT_SRC_DIR"), "15_About.cpp")
if os.path.exists(path):
    os.utime(path, None)
