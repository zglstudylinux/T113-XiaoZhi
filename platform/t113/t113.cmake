# T113 交叉工具链（gcc-830 glibc armhf，裁剪后入库 platform/t113/toolchain/）
#
# 用法：cmake -B build-t113 -DCMAKE_TOOLCHAIN_FILE=platform/t113/t113.cmake
#       （推荐经根目录 build.sh，勿裸跑 cmake --build）
# 注意：gcc.bin 的 specs 里 %:getenv(STAGING_DIR ...) 要求该环境变量存在
#       （值不必指向实际存在路径）；wrapper 本体用 readlink -f 自定位 sysroot，
#       可重定位。build.sh 已统一 export。
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(XZ_TOOLCHAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/toolchain" CACHE PATH "toolchain dir")

set(CMAKE_C_COMPILER   "${XZ_TOOLCHAIN_DIR}/bin/arm-openwrt-linux-gcc")
set(CMAKE_AR           "${XZ_TOOLCHAIN_DIR}/bin/arm-openwrt-linux-ar")

set(ENV{STAGING_DIR} "${XZ_TOOLCHAIN_DIR}")
