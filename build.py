import sys
import os

def err():
  print("\n\033[31;5m[easy] error input\033[0m")

if sys.platform.startswith("linux"):
  print("\033[36m[easy] please enter the index of the platform you want to compile\033[0m")

  target_path = os.path.dirname(os.path.realpath(__file__)) + "/target/"
  platform_list = os.listdir(target_path)
  platform_list.remove("windows")

  for i, file in enumerate(platform_list):
    print("\033[36m[easy] " + str(i) + ": " + file + "\033[0m")
    i = i + 1

  user_input = raw_input()
  if user_input.isdigit():
    user_input = (int)(user_input)
    if 0 <= user_input < len(platform_list):
      print("\033[36m[easy] build " + platform_list[user_input] + "\033[0m")
      os.system(target_path + platform_list[user_input] + "/build.sh")
    else:
      err()
  else:
    err()

elif sys.platform.startswith("win"):
  os.system("call target/windows/build.bat")
