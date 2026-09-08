# platform/t113/CMakeLists.txt — T113 平台层：
#   1) port 源（sunxifb + evdev + FreeType + tick）编成 xz_port 静态库
#   2) 第三方依赖解析：头/库全部来自仓库内 platform/t113/{include,lib}
#      （无 TINA_SDK_PATH / staging 外部依赖，克隆仓库即可交叉编译）
set(XZ_PLATFORM_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(XZ_T113_INC "${XZ_PLATFORM_DIR}/include")
set(XZ_T113_LIB "${XZ_PLATFORM_DIR}/lib")

# ---- 仓库内依赖完整性检查（缺失即 configure 失败）----
foreach(_f
        "${XZ_T113_LIB}/libopus.a"
        "${XZ_T113_LIB}/libfreetype.so"
        "${XZ_T113_LIB}/libssl.so"
        "${XZ_T113_LIB}/libcrypto.so"
        "${XZ_T113_LIB}/libasound.so"
        "${XZ_T113_LIB}/libspeexdsp.so"
        "${XZ_T113_LIB}/libz.so"
        "${XZ_T113_LIB}/libbz2.so")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "platform/t113/lib 缺文件: ${_f}")
    endif()
endforeach()

# ---- 平台头（alsa/openssl/opus/speex/ft2build+freetype）经 INTERFACE target 注入 ----
# 组件按需 PRIVATE 引用；不进全局 include，保持 app 层与平台头隔离。
add_library(xz_platform_headers INTERFACE)
target_include_directories(xz_platform_headers INTERFACE
    "${XZ_T113_INC}"
    "${XZ_T113_INC}/freetype")     # ft2build.h + freetype/ 所在目录

# ---- 依赖解析为 GLOBAL IMPORTED（顶层按固定顺序链接；.so 本体可链，.so.N 软链不行）----
add_library(xz::opus     UNKNOWN IMPORTED GLOBAL)
set_target_properties(xz::opus PROPERTIES IMPORTED_LOCATION "${XZ_T113_LIB}/libopus.a")

add_library(xz::freetype UNKNOWN IMPORTED GLOBAL)
set_target_properties(xz::freetype PROPERTIES IMPORTED_LOCATION "${XZ_T113_LIB}/libfreetype.so")

add_library(xz::ssl      UNKNOWN IMPORTED GLOBAL)
set_target_properties(xz::ssl PROPERTIES IMPORTED_LOCATION "${XZ_T113_LIB}/libssl.so")

add_library(xz::crypto   UNKNOWN IMPORTED GLOBAL)
set_target_properties(xz::crypto PROPERTIES IMPORTED_LOCATION "${XZ_T113_LIB}/libcrypto.so")

add_library(xz::alsa     UNKNOWN IMPORTED GLOBAL)
set_target_properties(xz::alsa PROPERTIES IMPORTED_LOCATION "${XZ_T113_LIB}/libasound.so")

add_library(xz::speexdsp UNKNOWN IMPORTED GLOBAL)
set_target_properties(xz::speexdsp PROPERTIES IMPORTED_LOCATION "${XZ_T113_LIB}/libspeexdsp.so")

# t113 无 SDL：占位空 target，保证顶层链接列表平台无关
add_library(xz::sdl INTERFACE IMPORTED GLOBAL)

# ---- port 源（sunxifb + evdev + FreeType 三字号 + tick）----
file(GLOB XZ_PORT_SRC "${XZ_PLATFORM_DIR}/port/*.c")
add_library(xz_port STATIC ${XZ_PORT_SRC})
target_include_directories(xz_port PRIVATE
    "${XZ_PLATFORM_DIR}/port"
    "${CMAKE_SOURCE_DIR}/ui"          # ui_res.h
    "${CMAKE_SOURCE_DIR}/third_party" # lv_drivers/...、lvgl/src/.../png
)
target_link_libraries(xz_port PUBLIC xz_osal)   # osal_log/osal_time（连带 include 传播）
