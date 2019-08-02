import sys
import os
import glob
import shutil

pwd = os.path.dirname(os.path.realpath(__file__))

for outpath in glob.glob(os.path.join(pwd, "out_*")):
  shutil.rmtree(outpath)

for root, dirs, files in os.walk(pwd):
  for name in files:
    if(name.endswith(".pyc")):
      os.remove(os.path.join(root, name))

if sys.platform.startswith("linux"):
  if os.path.exists(pwd + "/sysroot/debian_sid_amd64-sysroot/"):
    shutil.rmtree(pwd + "/sysroot/debian_sid_amd64-sysroot/")

if sys.platform.startswith("linux"):
  print('\033[32;5m[easy] clean successful\033[0m')
elif sys.platform.startswith("win"):
  print("[easy] clean successful")
