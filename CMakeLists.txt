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
    ${CMAKE_SOURCE_DIR}/port
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
# 显示后端宏全局注入（lv_drv_conf.h 的 USE_SDL/USE_SUNXIFB/USE_EVDEV/
# SDL_HOR_RES 均为 #ifndef 守卫，可被 -D 覆盖）。必须全局：port_display.c
#（xiaozhi target）与 lv_drivers 自身都要看到同一套宏。
# host（PC 模拟器）：SDL 窗口 480×640 竖屏（与板同尺寸）。
if(XZ_PLATFORM STREQUAL "host")
    add_compile_definitions(USE_SDL=1 USE_SUNXIFB=0 SDL_HOR_RES=480 SDL_VER_RES=640)
else()
    add_compile_definitions(USE_SUNXIFB=1 USE_EVDEV=1)
endif()

add_library(cjson STATIC ${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c)

# ---- OSAL（接口 + POSIX 参考实现；MCU 侧换 posix/ 目录即可）----
file(GLOB OSAL_SRC ${CMAKE_SOURCE_DIR}/osal/posix/*.c)
add_library(xz_osal STATIC ${OSAL_SRC})
target_include_directories(xz_osal PUBLIC ${CMAKE_SOURCE_DIR}/osal/include)

# ---- 应用源码（新分层布局）----
file(GLOB APP_SRC
    ${CMAKE_SOURCE_DIR}/main/*.c
    ${CMAKE_SOURCE_DIR}/app/*.c
    ${CMAKE_SOURCE_DIR}/app/net/*.c
    ${CMAKE_SOURCE_DIR}/app/audio/*.c
    ${CMAKE_SOURCE_DIR}/app/wifi/*.c
    ${CMAKE_SOURCE_DIR}/ui/*.c)
if(XZ_PLATFORM STREQUAL "t113")
    list(APPEND APP_SRC ${CMAKE_SOURCE_DIR}/port/linux/port_display.c
                        ${CMAKE_SOURCE_DIR}/port/linux/port_tick.c)
endif()
if(XZ_PLATFORM STREQUAL "host")
    list(APPEND APP_SRC ${CMAKE_SOURCE_DIR}/port/sdl/port_display.c
                        ${CMAKE_SOURCE_DIR}/port/sdl/port_tick.c)
endif()

add_executable(xiaozhi ${APP_SRC})
# 资源路径编译期兜底：板上=部署目录；host=仓库 assets（运行时 ui_res 会先
# 探测 env/exe 同目录/上级 assets，见 ui/ui_res.c）
if(XZ_PLATFORM STREQUAL "t113")
    target_compile_definitions(xiaozhi PRIVATE XZ_RES_PATH="/mnt/UDISK/xiaozhi")
else()
    target_compile_definitions(xiaozhi PRIVATE XZ_RES_PATH="${CMAKE_SOURCE_DIR}/assets")
endif()
target_link_libraries(xiaozhi PRIVATE lvgl lv_drivers cjson xz_osal
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
