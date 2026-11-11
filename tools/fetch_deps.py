# tools/fetch_deps.py —— 手动拉取 DotUI 依赖（u8g2）并应用 SDL 模拟器补丁
# 用法：python tools/fetch_deps.py
# 说明：与 cmake/basic.cmake 的自动拉取逻辑一致，供不想让 CMake 隐式联网的用户显式调用
import subprocess
import sys
import os

# 与 cmake/basic.cmake 中 DOTUI_U8G2_TAG 保持一致（vendored 时的上游 commit）
U8G2_REPO = "https://github.com/olikraus/u8g2.git"
U8G2_TAG = "d6c8499c5f2707cac8eccd09fd8f677d12b17977"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "lib", "u8g2")
PATCH = os.path.join(ROOT, "patches", "u8g2-sdl-local.patch")


def run(cmd, cwd=None):
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def main():
    # 已存在则跳过（幂等）
    if os.path.exists(os.path.join(DEST, "csrc", "u8g2.h")):
        print("u8g2 已存在，跳过拉取")
        return
    # git clone --branch 不支持 commit hash，用 init + fetch origin <sha> 浅拉取
    run(["git", "init", DEST])
    run(["git", "-C", DEST, "remote", "add", "origin", U8G2_REPO])
    run(["git", "-C", DEST, "fetch", "--depth", "1", "origin", U8G2_TAG])
    run(["git", "-C", DEST, "checkout", "--quiet", "FETCH_HEAD"])
    # 应用 SDL 后端补丁（仅影响 example 模拟器）
    run(["git", "-C", DEST, "apply", PATCH])
    print("完成：u8g2 已就位于", DEST)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        print("拉取失败，退出码:", e.returncode)
        sys.exit(1)
