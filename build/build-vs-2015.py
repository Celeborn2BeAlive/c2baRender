import os
import shutil

SCRIPT_PATH = os.path.realpath(__file__)

def mkdir(path):
    if not os.path.exists(path):
        os.makedirs(path)

def copy(src, dst):
    if os.path.isdir(src):
        if os.path.exists(dst):
            copyfiles(src, dst)
        else:
            print("cp " + src + " -> " + dst)
            shutil.copytree(src, dst)
    else:
        print("cp " + src + " -> " + dst)
        shutil.copy(src, dst)

def copyfiles(dir, dstDir):
    for entry in os.listdir(dir):
        copy(os.path.join(dir, entry), os.path.join(dstDir, entry))

def git(command):
    os.system("git " + command)

print(SCRIPT_PATH)