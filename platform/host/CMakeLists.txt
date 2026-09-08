# platform/host/CMakeLists.txt — PC/SDL 模拟器平台层（依赖全走系统包，CI 即此路径）
set(XZ_PLATFORM_DIR "${CMAKE_CURRENT_LIST_DIR}")

# host 无平台私有头：同名的空 INTERFACE，组件引用才能平台无关
add_library(xz_platform_headers INTERFACE)

# 与 t113 分支同名同形的 xz::* target：系统库路径在 configure 期解析
find_library(XZ_HOST_OPUS     opus     REQUIRED)   # apt libopus-dev
find_library(XZ_HOST_SSL      ssl      REQUIRED)   # apt libssl-dev
find_library(XZ_HOST_CRYPTO   crypto   REQUIRED)
find_library(XZ_HOST_ALSA     asound   REQUIRED)   # apt libasound2-dev
find_library(XZ_HOST_SPEEXDSP speexdsp REQUIRED)   # apt libspeexdsp-dev
find_library(XZ_HOST_SDL2     SDL2     REQUIRED)   # apt libsdl2-dev（显示后端）
# freetype 用 find_package 拿 include 路径（/usr/include/freetype2）
find_package(Freetype REQUIRED)
find_library(XZ_HOST_FT freetype REQUIRED)

macro(_xz_import name path)
    add_library(xz::${name} UNKNOWN IMPORTED GLOBAL)
    set_target_properties(xz::${name} PROPERTIES IMPORTED_LOCATION "${path}")
endmacro()
_xz_import(opus     "${XZ_HOST_OPUS}")
_xz_import(freetype "${XZ_HOST_FT}")
set_target_properties(xz::freetype PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FREETYPE_INCLUDE_DIRS}")
_xz_import(ssl      "${XZ_HOST_SSL}")
_xz_import(crypto   "${XZ_HOST_CRYPTO}")
_xz_import(alsa     "${XZ_HOST_ALSA}")
_xz_import(speexdsp "${XZ_HOST_SPEEXDSP}")
_xz_import(sdl      "${XZ_HOST_SDL2}")

# port 源（SDL2 窗口 480x640 + 鼠标键盘 + tick）
file(GLOB XZ_PORT_SRC "${XZ_PLATFORM_DIR}/port/*.c")
add_library(xz_port STATIC ${XZ_PORT_SRC})
target_include_directories(xz_port PRIVATE
    "${XZ_PLATFORM_DIR}/port"
    "${CMAKE_SOURCE_DIR}/ui"
    "${CMAKE_SOURCE_DIR}/third_party")
target_link_libraries(xz_port PUBLIC xz_osal)
