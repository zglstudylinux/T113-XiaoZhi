# T113 AI 小智 — CMake 构建（P1：与旧 Makefile 等价迁移，src/ 布局原样不动）
#
# 用法（推荐经 scripts/build.sh，它负责 export STAGING_DIR）：
#   ./scripts/build.sh t113    # 交叉构建 → build-t113/xiaozhi
#   ./scripts/build.sh host    # PC 构建（SDL 模拟器）→ build-host/xiaozhi
#
cmake_minimum_required(VERSION 3.16)

# project() 之前：平台开关（工具链文件也会设 XZ_PLATFORM）
if(NOT DEFINED XZ_PLATFORM)
    if(DEFINED CMAKE_TOOLCHAIN_FILE)
        set(XZ_PLATFORM t113)
    else()
        set(XZ_PLATFORM host)
    endif()
endif()
set(XZ_PLATFORM "${XZ_PLATFORM}" CACHE STRING "t113 | host")

project(xiaozhi C)
set(CMAKE_C_STANDARD 99)

include(cmake/deps.cmake)

# ---- 编译选项（与旧 Makefile 对齐）----
if(XZ_PLATFORM STREQUAL "t113")
    add_compile_options(-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard)
endif()
add_compile_options(-Wall -Wno-unused-function -Wno-unused-parameter
    -Wno-missing-prototypes -Wno-sign-compare -Wno-format-nonliteral)

add_compile_definitions(LV_USE_PNG=1 LV_USE_FS_POSIX=1)

include_directories(
    ${CMAKE_SOURCE_DIR}/main
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/app/net
    ${CMAKE_SOURCE_DIR}/app/audio
    ${CMAKE_SOURCE_DIR}/app/wifi
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/third_party            # "lvgl/lvgl.h"、"lv_drv_conf.h"
    ${CMAKE_SOURCE_DIR}/third_party/lvgl/src/extra/libs/freetype
    ${CMAKE_SOURCE_DIR}/third_party/freetype/include
    ${CMAKE_SOURCE_DIR}/third_party/cjson
    ${XZ_INCLUDE_DIRS}
)

# ---- 第三方 ----
# lvgl 8.3.1：仓库只有 lvgl.mk（上游 CMakeLists 已删），自写 GLOB 等价编译。
# lv_conf.h 在 ui/，__has_include("lv_conf.h") 自动启用 LV_CONF_INCLUDE_SIMPLE。
file(GLOB_RECURSE LVGL_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/third_party/lvgl/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC ${CMAKE_SOURCE_DIR}/ui)

# lv_drivers：只编 display/indev（sdl/wayland/gtkdrv/win32drv 被 lv_drv_conf 守卫清空，
# host 平台显式加 sdl 两文件）；lv_drv_conf.h 由 third_party include 路径提供。
file(GLOB LVDRV_SRC
    ${CMAKE_SOURCE_DIR}/third_party/lv_drivers/display/*.c
    ${CMAKE_SOURCE_DIR}/third_party/lv_drivers/indev/*.c)
if(XZ_PLATFORM STREQUAL "host")
    list(APPEND LVDRV_SRC
        ${CMAKE_SOURCE_DIR}/third_party/lv_drivers/sdl/sdl.c
        ${CMAKE_SOURCE_DIR}/third_party/lv_drivers/sdl/sdl_common.c)
endif()
add_library(lv_drivers STATIC ${LVDRV_SRC})
target_compile_definitions(lv_drivers PRIVATE
    $<IF:$<STREQUAL:${XZ_PLATFORM},host>,USE_SDL=1 USE_SUNXIFB=0,USE_SUNXIFB=1 USE_EVDEV=1>)

add_library(cjson STATIC ${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c)

# ---- 应用源码（新分层布局）----
file(GLOB APP_SRC
    ${CMAKE_SOURCE_DIR}/main/*.c
    ${CMAKE_SOURCE_DIR}/app/*.c
    ${CMAKE_SOURCE_DIR}/app/net/*.c
    ${CMAKE_SOURCE_DIR}/app/audio/*.c
    ${CMAKE_SOURCE_DIR}/app/wifi/*.c
    ${CMAKE_SOURCE_DIR}/ui/*.c)

add_executable(xiaozhi ${APP_SRC})
target_link_libraries(xiaozhi PRIVATE lvgl lv_drivers cjson
    ${XZ_OPUS_LIB}                 # 静态链入（_libs）
    ${XZ_FREETYPE_LIB}
    ${XZ_SSL_LIB} ${XZ_CRYPTO_LIB}
    ${XZ_ALSA_LIB} ${XZ_SPEEX_LIB}
    ${XZ_SDL_LIB}
    z bz2 pthread rt dl m)
if(XZ_PLATFORM STREQUAL "t113")
    # z/bz2 是 freetype 的依赖：t113 用 third_party 预编译副本，host 用系统库
    target_link_directories(xiaozhi PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/freetype/lib ${XZ_STAGING_DIR}/lib)
    target_link_options(xiaozhi PRIVATE -Wl,-rpath,/usr/lib)
endif()
