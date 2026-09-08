# T113 AI 小智 — CMake 构建（R2：component/platform 排版，对齐 app_sdk）
#
# 用法（经根目录 build.sh，它负责 export STAGING_DIR）：
#   ./build.sh t113    # 交叉构建 → build-t113/xiaozhi（工具链在 platform/t113/toolchain）
#   ./build.sh host    # PC 构建（SDL 模拟器）→ build-host/xiaozhi
#
cmake_minimum_required(VERSION 3.18)

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

# ---- 编译选项（与旧 Makefile 对齐）----
if(XZ_PLATFORM STREQUAL "t113")
    add_compile_options(-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard)
endif()
add_compile_options(-Wall -Wno-unused-function -Wno-unused-parameter
    -Wno-missing-prototypes -Wno-sign-compare -Wno-format-nonliteral)

add_compile_definitions(LV_USE_PNG=1 LV_USE_FS_POSIX=1)

# ---- 全局 include（跨层前缀式引用的唯一定义点；源码 #include 零改动）----
#   ${CMAKE_SOURCE_DIR}       : "app/app_api.h"、"app_config.h"
#   component                 : "net/xz_protocol.h"、"wifi/wifi_manager.h" 前缀 include
#   component/hal             : "port_display.h"（hal 头独立目录）
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/main
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/component
    ${CMAKE_SOURCE_DIR}/component/hal
    ${CMAKE_SOURCE_DIR}/third_party            # "lvgl/lvgl.h"、"lv_drv_conf.h"
    ${CMAKE_SOURCE_DIR}/third_party/lvgl/src/extra/libs/freetype   # "lv_freetype.h"
    ${CMAKE_SOURCE_DIR}/third_party/cjson      # "cJSON.h"
)

# ft2build.h：lvgl 的 lv_freetype.c 全局需要（t113=仓库内；host=系统包）
if(XZ_PLATFORM STREQUAL "t113")
    include_directories(${CMAKE_SOURCE_DIR}/platform/t113/include)
else()
    find_package(Freetype REQUIRED)
    include_directories(${FREETYPE_INCLUDE_DIRS})
endif()

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
# SDL_HOR_RES 均为 #ifndef 守卫，可被 -D 覆盖）。必须全局：xz_port 的
# port_display.c 与 lv_drivers 自身都要看到同一套宏。
# host（PC 模拟器）：SDL 窗口 480×640 竖屏（与板同尺寸）。
if(XZ_PLATFORM STREQUAL "host")
    add_compile_definitions(USE_SDL=1 USE_SUNXIFB=0 SDL_HOR_RES=480 SDL_VER_RES=640)
else()
    add_compile_definitions(USE_SUNXIFB=1 USE_EVDEV=1)
endif()

add_library(cjson STATIC ${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c)

# ---- 平台层先于组件：提供 xz_platform_headers / xz::* IMPORTED target / xz_port ----
add_subdirectory(platform/${XZ_PLATFORM})

# ---- 组件（STATIC 库，互相只传 include 不链库；符号由最终链接解析）----
add_subdirectory(component/osal)
add_subdirectory(component/net)
add_subdirectory(component/audio)
add_subdirectory(component/wifi)

# ---- 应用可执行（main/ app/ ui/ 常驻；port 源在 xz_port 里）----
file(GLOB XZ_APP_SRC
    ${CMAKE_SOURCE_DIR}/main/*.c
    ${CMAKE_SOURCE_DIR}/app/*.c
    ${CMAKE_SOURCE_DIR}/ui/*.c)
add_executable(xiaozhi ${XZ_APP_SRC})

# 资源路径编译期兜底：板上=部署目录；host=仓库 assets（运行时 ui_res 会先
# 探测 env/exe 同目录/上级 assets，见 ui/ui_res.c）
if(XZ_PLATFORM STREQUAL "t113")
    target_compile_definitions(xiaozhi PRIVATE
        XZ_RES_PATH="/mnt/UDISK/xiaozhi" XZ_DATA_DIR="/mnt/UDISK/xiaozhi")
else()
    target_compile_definitions(xiaozhi PRIVATE
        XZ_RES_PATH="${CMAKE_SOURCE_DIR}/assets"
        XZ_DATA_DIR="${CMAKE_SOURCE_DIR}/assets")
endif()

# 链接顺序是契约（改动前先 diff readelf -d 的 NEEDED，基线 12 项）。
#   - 静态库按"依赖者在前、被依赖者在后"排列：xz_osal 被所有组件引用，
#     必须在它们之后（否则其成员在扫描时未被引用不被拉入，拖到行尾重复
#     项才拉入时 libssl 已扫过，--as-needed 下 SSL_* 解析失败）；
#   - lvgl↔xz_port↔lv_drivers 互依（lvgl 引用 xz_port 的 custom_tick_get；
#     xz_port 引用 lv_drivers 的 sunxifb/evdev），用 group 包住多遍解析；
#   - 共享库顺序 opus → freetype → ssl/crypto → asound/speexdsp → sdl(占位)
#     → z/bz2/pthread/rt/dl/m。
target_link_libraries(xiaozhi PRIVATE
    xz_net xz_audio xz_wifi cjson
    "-Wl,--start-group" lvgl lv_drivers xz_port "-Wl,--end-group"
    xz_osal
    xz::opus xz::freetype xz::ssl xz::crypto xz::alsa xz::speexdsp xz::sdl
    z bz2 pthread rt dl m)

if(XZ_PLATFORM STREQUAL "t113")
    # z/bz2 是 freetype 的依赖，库文件全部在仓库 platform/t113/lib
    target_link_directories(xiaozhi PRIVATE ${CMAKE_SOURCE_DIR}/platform/t113/lib)
    target_link_options(xiaozhi PRIVATE -Wl,-rpath,/usr/lib)
endif()
