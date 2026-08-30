# T113 交叉工具链（gcc-830 glibc armhf，scripts/setup.sh 复制到 toolchain/）
#
# 用法：cmake -B build-t113 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-t113.cmake
# 注意：OpenWrt wrapper gcc 编译期要求 STAGING_DIR 指向工具链目录，
#       由 scripts/build.sh export 后再调 cmake（勿裸跑 ninja）。
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(XZ_TOOLCHAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../toolchain" CACHE PATH "toolchain dir")

set(CMAKE_C_COMPILER   "${XZ_TOOLCHAIN_DIR}/bin/arm-openwrt-linux-gcc")
set(CMAKE_AR           "${XZ_TOOLCHAIN_DIR}/bin/arm-openwrt-linux-ar")

# wrapper gcc 的 STAGING_DIR 语义与旧 Makefile 的 export STAGING_DIR 一致
set(ENV{STAGING_DIR} "${XZ_TOOLCHAIN_DIR}")
