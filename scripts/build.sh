#!/bin/bash
# scripts/build.sh — 双平台一键构建（唯一推荐入口）
#
# 用法：
#   ./scripts/build.sh t113    # 交叉构建 → build-t113/xiaozhi（需先跑 scripts/setup.sh）
#   ./scripts/build.sh host    # PC 构建（SDL 模拟器）→ build-host/xiaozhi
#   ./scripts/build.sh clean   # 删除两个构建目录
#
# 为什么不直接裸跑 cmake/ninja：OpenWrt wrapper gcc（toolchain/bin/arm-openwrt-linux-gcc）
# 编译期要求环境变量 STAGING_DIR 指向工具链目录，本脚本负责 export。
set -e
cd "$(dirname "$0")/.."

TINA_SDK_PATH="${TINA_SDK_PATH:-/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2}"
export STAGING_DIR="$PWD/toolchain"

case "$1" in
t113)
    [ -x "$STAGING_DIR/bin/arm-openwrt-linux-gcc" ] || {
        echo "错误: toolchain/ 缺失，先跑 TINA_SDK_PATH=$TINA_SDK_PATH ./scripts/setup.sh"; exit 1; }
    cmake -B build-t113 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-t113.cmake \
        -DTINA_SDK_PATH="$TINA_SDK_PATH"
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
