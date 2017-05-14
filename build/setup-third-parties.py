import os
import shutil
from contextlib import contextmanager

SCRIPT_PATH = os.path.realpath(__file__)
ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_PATH))
THIRD_PARTY_DIR = os.path.join(ROOT_DIR, "third-party")

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

@contextmanager
def pushd(newDir):
    previousDir = os.getcwd()
    os.chdir(newDir)
    yield
    os.chdir(previousDir)

GLFW = {}
GLFW["git_repo"] = "https://github.com/glfw/glfw"
GLFW["version"] = "3.2.1"
GLFW["prefix"] = "glfw"
GLFW["git_tag"] = GLFW["version"]

GLM = {}
GLM["git_repo"] = "https://github.com/g-truc/glm"
GLM["version"] = "0.9.8.4"
GLM["prefix"] = "glm"
GLM["git_tag"] = GLM["version"]

IMGUI = {}
IMGUI["git_repo"] = "https://github.com/ocornut/imgui"
IMGUI["version"] = "1.49"
IMGUI["prefix"] = "imgui"
IMGUI["git_tag"] = "v" + IMGUI["version"]

ASSIMP = {}
ASSIMP["git_repo"] = "https://github.com/assimp/assimp"
ASSIMP["version"] = "3.3.1"
ASSIMP["prefix"] = "assimp"
ASSIMP["git_tag"] = "v" + ASSIMP["version"]

JSON = {}
JSON["git_repo"] = "https://github.com/nlohmann/json"
JSON["version"] = "2.1.1"
JSON["prefix"] = "json"
JSON["git_tag"] = "v" + JSON["version"]

EMBREE = {}
EMBREE["git_repo"] = "https://github.com/embree/embree"
EMBREE["version"] = "2.15.0"
EMBREE["prefix"] = "embree"
EMBREE["git_tag"] = "v" + EMBREE["version"]

OPENEXR = {}
OPENEXR["git_repo"] = "https://github.com/openexr/openexr"
OPENEXR["version"] = "2.2.0"
OPENEXR["prefix"] = "openexr"
OPENEXR["git_tag"] = "v" + OPENEXR["version"]

STB = {}
STB["git_repo"] = "https://github.com/nothings/stb"
STB["version"] = "1.0"
STB["prefix"] = "stb"

git_repos = [ GLFW, GLM, IMGUI, ASSIMP, JSON, EMBREE, OPENEXR, STB ]

for repo in git_repos:
    directory = os.path.join(THIRD_PARTY_DIR, repo["prefix"])
    if not os.path.exists(directory):
        os.makedirs(directory)
        with pushd(directory):
            git("clone " + repo["git_repo"] + " " + directory)
            if "git_tag" in repo:
                git("checkout " + repo["git_tag"])