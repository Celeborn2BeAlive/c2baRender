import os
import shutil
from contextlib import contextmanager

SCRIPT_PATH = os.path.realpath(__file__)
SCRIPT_DIR = os.path.dirname(SCRIPT_PATH)
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
THIRD_PARTY_DIR = os.path.join(ROOT_DIR, "third-party")

PATH_VSBUILD = os.path.join(SCRIPT_DIR, "build-vs.bat")

BUILD_TYPES = [ "Release", "Debug" ]

VS_VERSIONS = {"2012": "11", "2015": "14"}
VS_YEAR = "2015"
VS_VERSION = VS_VERSIONS[VS_YEAR]
BUILD_DIR_SUFFIX = [ "-vs-" + VS_YEAR + "-" + build_type for build_type in BUILD_TYPES ]
CMAKE_GENERATOR = "Visual Studio " + VS_VERSION + " " + VS_YEAR + " Win64"

CMAKE_INSTALL_PREFIX = [ os.path.join(THIRD_PARTY_DIR, "dist", build_type) for build_type in BUILD_TYPES ]

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

def cmake(command):
    os.system('cmake -G "' + CMAKE_GENERATOR + '" ' + command)

def cmake_default(build_type, install_prefix, directory, additional_commands = ""):
    cmake(" -DCMAKE_BUILD_TYPE=" + build_type +
          " -DCMAKE_INSTALL_PREFIX=" + install_prefix +
          " -DCMAKE_PREFIX_PATH=" + install_prefix +
          " -DCMAKE_CXX_FLAGS_RELEASE=/Zi" +
          " -DCMAKE_SHARED_LINKER_FLAGS_RELEASE=/DEBUG /OPT:REF /OPT:ICF " +
          additional_commands +
          " " + directory)

def build(file, config):
    os.system(PATH_VSBUILD + " " + file + " " + config + " " + VS_VERSION)

@contextmanager
def pushd(newDir):
    previousDir = os.getcwd()
    os.chdir(newDir)
    yield
    os.chdir(previousDir)

def get_clone_dir(lib):
    return os.path.join(THIRD_PARTY_DIR, "src", repo["prefix"])

def build_zlib(lib_info):
    CLONE_DIR = get_clone_dir(lib_info)
    LIB_NAME = lib_info["prefix"]
    BUILD_DIR = [ os.path.join(THIRD_PARTY_DIR, "build", LIB_NAME + "-" + x) for x in BUILD_TYPES ]

    for i, build_type in enumerate(BUILD_TYPES):
        if not os.path.exists(BUILD_DIR[i]):
            mkdir(BUILD_DIR[i])
            with pushd(BUILD_DIR[i]):
                cmake_default(build_type, CMAKE_INSTALL_PREFIX[i], CLONE_DIR)
        with pushd(BUILD_DIR[i]):
            build("INSTALL.vcxproj", build_type)
            if build_type == "Release":
                copy(BUILD_DIR[i] + "/" + build_type + "/zlib.pdb", CMAKE_INSTALL_PREFIX[i] + "/lib/zlib.pdb")
            else:
                copy(BUILD_DIR[i] + "/" + build_type + "/zlibd.pdb", CMAKE_INSTALL_PREFIX[i] + "/lib/zlibd.pdb")

def build_openexr(lib_info):
    CLONE_DIR = get_clone_dir(lib_info)
    for LIB_NAME in [ "Ilmbase", "OpenEXR" ]:
        BUILD_DIR = [ os.path.join(THIRD_PARTY_DIR, "build", LIB_NAME + "-" + x) for x in BUILD_TYPES ]

        for i, build_type in enumerate(BUILD_TYPES):
            if not os.path.exists(BUILD_DIR[i]):
                mkdir(BUILD_DIR[i])
                with pushd(BUILD_DIR[i]):
                    if LIB_NAME == "Ilmbase":
                        cmake_default(build_type, CMAKE_INSTALL_PREFIX[i], CLONE_DIR + "/IlmBase")
                    else:
                        cmake_default(build_type, CMAKE_INSTALL_PREFIX[i], CLONE_DIR + "/OpenEXR",
                            " -DILMBASE_PACKAGE_PREFIX=" + CMAKE_INSTALL_PREFIX[i] + 
                            " -DZLIB_ROOT=" + CMAKE_INSTALL_PREFIX[i])
                        mkdir("IlmImf/"+build_type)
                        copy(CMAKE_INSTALL_PREFIX[i]+"/lib/Half.dll", "IlmImf/"+build_type+"/Half.dll")
                        copy(CMAKE_INSTALL_PREFIX[i]+"/lib/Iex-2_2.dll", "IlmImf/"+build_type+"/Iex-2_2.dll")
                        copy(CMAKE_INSTALL_PREFIX[i]+"/lib/IlmThread-2_2.dll", "IlmImf/"+build_type+"/IlmThread-2_2.dll")
            with pushd(BUILD_DIR[i]):
                build("INSTALL.vcxproj", build_type)

                if LIB_NAME == "Ilmbase":
                    copy(BUILD_DIR[i] + "/Half/" + build_type + "/Half.pdb", CMAKE_INSTALL_PREFIX[i] + "/lib/Half.pdb")
                    copy(BUILD_DIR[i] + "/Iex/" + build_type + "/Iex-2_2.pdb", CMAKE_INSTALL_PREFIX[i] + "/lib/Iex-2_2.pdb")
                    copy(BUILD_DIR[i] + "/IlmThread/" + build_type + "/IlmThread-2_2.pdb", CMAKE_INSTALL_PREFIX[i] + "/lib/IlmThread-2_2.pdb")
                    copy(BUILD_DIR[i] + "/Imath/" + build_type + "/Imath-2_2.pdb", CMAKE_INSTALL_PREFIX[i] + "/lib/Imath-2_2.pdb")
                else:
                    copy(BUILD_DIR[i] + "/IlmImf/" + build_type + "/IlmImf-2_2.pdb", CMAKE_INSTALL_PREFIX[i] + "/lib/IlmImf-2_2.pdb")

def build_embree(lib_info):
    CLONE_DIR = get_clone_dir(lib_info)
    LIB_NAME = lib_info["prefix"]
    BUILD_DIR = [ os.path.join(THIRD_PARTY_DIR, "build", LIB_NAME + "-" + x) for x in BUILD_TYPES ]

    for i, build_type in enumerate(BUILD_TYPES):
        if not os.path.exists(BUILD_DIR[i]):
            mkdir(BUILD_DIR[i])
            with pushd(BUILD_DIR[i]):
                cmake_default(build_type, CMAKE_INSTALL_PREFIX[i], CLONE_DIR,
                    " -DEMBREE_TASKING_SYSTEM=INTERNAL" +
                    " -DEMBREE_RAY_MASK=1" + 
                    " -DEMBREE_ISPC_SUPPORT=0" + 
                    " -DEMBREE_STATIC_LIB=0")
        with pushd(BUILD_DIR[i]):
            build("INSTALL.vcxproj", build_type)
            copy(BUILD_DIR[i] + "/" + build_type + "/embree.pdb", CMAKE_INSTALL_PREFIX[i] + "/lib/embree.pdb")

def default_build(lib_info):
    CLONE_DIR = get_clone_dir(lib_info)
    LIB_NAME = lib_info["prefix"]
    BUILD_DIR = [ os.path.join(THIRD_PARTY_DIR, "build", LIB_NAME + "-" + x) for x in BUILD_TYPES ]

    for i, build_type in enumerate(BUILD_TYPES):
        if not os.path.exists(BUILD_DIR[i]):
            mkdir(BUILD_DIR[i])
            with pushd(BUILD_DIR[i]):
                cmake_default(build_type, CMAKE_INSTALL_PREFIX[i], CLONE_DIR)
        with pushd(BUILD_DIR[i]):
            build("INSTALL.vcxproj", build_type)

ZLIB = {}
ZLIB["git_repo"] = "https://github.com/madler/zlib"
ZLIB["version"] = "1.2.11"
ZLIB["prefix"] = "zlib"
ZLIB["git_tag"] = "v" + ZLIB["version"]
ZLIB["build"] = build_zlib

GLFW = {}
GLFW["git_repo"] = "https://github.com/glfw/glfw"
GLFW["version"] = "3.2.1"
GLFW["prefix"] = "glfw"
GLFW["git_tag"] = GLFW["version"]
GLFW["build"] = default_build

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
ASSIMP["build"] = default_build

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
EMBREE["build"] = build_embree

OPENEXR = {}
OPENEXR["git_repo"] = "https://github.com/openexr/openexr"
OPENEXR["version"] = "2.2.0"
OPENEXR["prefix"] = "openexr"
OPENEXR["git_tag"] = "v" + OPENEXR["version"]
OPENEXR["build"] = build_openexr

STB = {}
STB["git_repo"] = "https://github.com/nothings/stb"
STB["version"] = "1.0"
STB["prefix"] = "stb"

git_repos = [ ZLIB, GLFW, GLM, IMGUI, ASSIMP, JSON, EMBREE, OPENEXR, STB ]

for repo in git_repos:
    directory = get_clone_dir(repo)
    if not os.path.exists(directory):
        os.makedirs(directory)
        with pushd(directory):
            git("clone " + repo["git_repo"] + " " + directory)
            if "git_tag" in repo:
                git("checkout " + repo["git_tag"])
    with pushd(directory):
        if "build" in repo:
            repo["build"](repo)