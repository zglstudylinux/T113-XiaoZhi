#!/bin/bash
#
# setup.sh — 首次准备：从本机 Tina SDK 复制交叉编译工具链 + 交叉编译 libopus
#
# toolchain/ 与 _libs/ 不进 git（.gitignore），克隆仓库后运行本脚本。
# 源路径可用环境变量 TINA_SDK_PATH 覆盖。
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"

TINA_SDK_PATH="${TINA_SDK_PATH:-/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2}"
TOOLCHAIN_SRC="$TINA_SDK_PATH/prebuilt/rootfsbuilt/arm/toolchain-sunxi-glibc-gcc-830"
TOOLCHAIN_DST="$PROJ_DIR/toolchain"

# ---------- 1. 工具链 ----------
if [ -x "$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc" ]; then
    echo "[setup] toolchain 已存在：$TOOLCHAIN_DST（跳过复制）"
else
    if [ ! -d "$TOOLCHAIN_SRC" ]; then
        echo "[setup] 错误：找不到工具链源目录：$TOOLCHAIN_SRC"
        echo "[setup] 请确认 Tina SDK 路径，或用 TINA_SDK_PATH=<路径> $0 指定"
        exit 1
    fi
    echo "[setup] 复制工具链（约 1.2GB，需几分钟）..."
    mkdir -p "$TOOLCHAIN_DST"
    cp -a "$TOOLCHAIN_SRC/toolchain/." "$TOOLCHAIN_DST/"
    echo "[setup] 完成：$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc"
fi
"$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc" --version | head -1

# ---------- 2. 板上库头文件（alsa / speexdsp / openssl，rootfs 动态库已存在） ----------
STAGING_INC="$TINA_SDK_PATH/out/t113/zgl_board/openwrt/staging_dir/target/usr/include"
INC_DST="$PROJ_DIR/_libs/include"
mkdir -p "$INC_DST"
for d in alsa speex speexdsp openssl; do
    if [ -d "$STAGING_INC/$d" ] && [ ! -d "$INC_DST/$d" ]; then
        cp -a "$STAGING_INC/$d" "$INC_DST/"
        echo "[setup] 复制头文件：$d"
    fi
done

# ---------- 3. 交叉编译 libopus（1.3.1，源码包在 SDK openwrt/dl/） ----------
OPUS_TARBALL="$TINA_SDK_PATH/openwrt/dl/opus-1.3.1.tar.gz"
OPUS_BUILD="$PROJ_DIR/_libs/opus-build"
OPUS_OUT="$PROJ_DIR/_libs/lib/libopus.a"

if [ -f "$OPUS_OUT" ]; then
    echo "[setup] libopus 已存在：$OPUS_OUT（跳过编译）"
    exit 0
fi
if [ ! -f "$OPUS_TARBALL" ]; then
    echo "[setup] 错误：找不到 opus 源码包：$OPUS_TARBALL"
    exit 1
fi

echo "[setup] 编译 libopus（gcc-830 交叉）..."
rm -rf "$OPUS_BUILD"
mkdir -p "$OPUS_BUILD"
tar -xf "$OPUS_TARBALL" -C "$OPUS_BUILD" --strip-components=1

export STAGING_DIR="$TOOLCHAIN_DST/"
cd "$OPUS_BUILD"
./configure \
    --host=arm-openwrt-linux \
    CC="$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc" \
    AR="$TOOLCHAIN_DST/bin/arm-openwrt-linux-ar" \
    RANLIB="$TOOLCHAIN_DST/bin/arm-openwrt-linux-ranlib" \
    CFLAGS="-O2 -march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard" \
    --disable-shared --enable-static \
    --disable-doc --disable-extra-programs
make -j"$(nproc)" libopus.la   # 只编库不编工具（host 工具会编失败）

mkdir -p "$PROJ_DIR/_libs/lib" "$INC_DST/opus"
cp .libs/libopus.a "$PROJ_DIR/_libs/lib/"
cp include/*.h "$INC_DST/opus/"

echo "[setup] libopus 完成：$OPUS_OUT"
"$TOOLCHAIN_DST/bin/arm-openwrt-linux-readelf" -h "$OPUS_OUT" | grep Machine
