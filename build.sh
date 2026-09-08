#!/bin/bash
# build.sh — 双平台一键构建（唯一推荐入口）
#
# 用法：
#   ./build.sh t113    # 交叉构建 → build-t113/xiaozhi（工具链在 platform/t113/toolchain）
#   ./build.sh host    # PC 构建（SDL 模拟器）→ build-host/xiaozhi
#   ./build.sh clean   # 删除两个构建目录
#
# 为什么不直接裸跑 cmake/ninja：OpenWrt wrapper gcc（platform/t113/toolchain/bin/）
# 的 specs 里 %:getenv(STAGING_DIR ...) 要求环境变量 STAGING_DIR 存在，
# 本脚本负责 export（cmake configure 与 --build 都在其后，env 继承）。
set -e
cd "$(dirname "$0")"
ROOT="$PWD"

export STAGING_DIR="$ROOT/platform/t113/toolchain"

case "$1" in
t113)
    [ -x "$STAGING_DIR/bin/arm-openwrt-linux-gcc" ] || {
        echo "错误: platform/t113/toolchain 缺失或不完整"; exit 1; }
    cmake -B build-t113 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_TOOLCHAIN_FILE=platform/t113/t113.cmake
    cmake --build build-t113 -j"$(nproc)"
    echo "===> build-t113/xiaozhi"
    ;;
host)
    cmake -B build-host -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXZ_PLATFORM=host
    cmake --build build-host -j"$(nproc)"
    echo "===> build-host/xiaozhi（SDL 模拟器）"
    ;;
clean)
    rm -rf build-t113 build-host
    ;;
*)
    echo "用法: $0 {t113|host|clean}"; exit 1
    ;;
esac
