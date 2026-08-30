# cmake/deps.cmake — 按平台解析第三方依赖（opus/freetype/ssl/alsa/speexdsp）
#
# t113 : opus 用 scripts/setup.sh 交叉编译产物 _libs/lib/libopus.a（静态）；
#        freetype 用 third_party 预编译 so（IMPORTED）；
#        ssl/crypto/alsa/speexdsp 用 Tina SDK staging（XZ_STAGING_DIR，不再硬编码）。
# host : 全部走系统包（apt：libopus-dev libfreetype-dev libssl-dev libasound2-dev
#        libspeexdsp-dev libsdl2-dev），CI 即此路径。
#
# 产出变量：XZ_OPUS_LIB XZ_FREETYPE_LIB XZ_SSL_LIB XZ_CRYPTO_LIB
#           XZ_ALSA_LIB XZ_SPEEX_LIB XZ_SDL_LIB XZ_INCLUDE_DIRS

set(XZ_INCLUDE_DIRS "")

if(XZ_PLATFORM STREQUAL "t113")
    # SDK staging：默认从 TINA_SDK_PATH（cache 变量或环境变量）推导（旧 Makefile 硬编码的替代）
    if(NOT TINA_SDK_PATH)
        set(TINA_SDK_PATH "$ENV{TINA_SDK_PATH}")
    endif()
    set(XZ_STAGING_DIR
        "${TINA_SDK_PATH}/out/t113/zgl_board/openwrt/staging_dir/target/usr"
        CACHE PATH "Tina SDK staging dir (.../staging_dir/target/usr)")

    if(NOT EXISTS "${XZ_STAGING_DIR}/lib")
        message(FATAL_ERROR
            "XZ_STAGING_DIR 不存在: ${XZ_STAGING_DIR}\n"
            "请设置 TINA_SDK_PATH 环境变量或 -DXZ_STAGING_DIR=<.../staging_dir/target/usr>")
    endif()

    # opus 静态库（setup.sh 交叉编译产物）
    find_library(XZ_OPUS_LIB opus
        PATHS "${CMAKE_SOURCE_DIR}/_libs/lib" NO_DEFAULT_PATH REQUIRED)
    # staging 头（alsa/openssl/speexdsp；opus 头在 _libs/include 里版本一致）
    list(APPEND XZ_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/_libs/include")
    list(APPEND XZ_INCLUDE_DIRS "${XZ_STAGING_DIR}/include")

    # freetype 预编译 so（IMPORTED；.so 本体可链，.so.6 软链不行）
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/freetype/lib/libfreetype.so")
        message(FATAL_ERROR "third_party/freetype/lib/libfreetype.so 不存在")
    endif()
    add_library(freetype::ft UNKNOWN IMPORTED)
    set_target_properties(freetype::ft PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/freetype/lib/libfreetype.so")
    set(XZ_FREETYPE_LIB freetype::ft)

    find_library(XZ_SSL_LIB     ssl      PATHS "${XZ_STAGING_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(XZ_CRYPTO_LIB  crypto   PATHS "${XZ_STAGING_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(XZ_ALSA_LIB    asound   PATHS "${XZ_STAGING_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(XZ_SPEEX_LIB   speexdsp PATHS "${XZ_STAGING_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
    # host 平台的 SDL 在此分支不存在
    set(XZ_SDL_LIB "")

else() # host
    find_library(XZ_OPUS_LIB opus REQUIRED)          # apt libopus-dev
    find_package(Freetype REQUIRED)
    set(XZ_FREETYPE_LIB Freetype::Freetype)
    find_library(XZ_SSL_LIB ssl REQUIRED)            # apt libssl-dev
    find_library(XZ_CRYPTO_LIB crypto REQUIRED)
    find_library(XZ_ALSA_LIB asound REQUIRED)        # apt libasound2-dev
    find_library(XZ_SPEEX_LIB speexdsp REQUIRED)     # apt libspeexdsp-dev
    find_library(XZ_SDL_LIB SDL2)                    # apt libsdl2-dev（模拟器）
    if(NOT XZ_SDL_LIB)
        message(FATAL_ERROR "host 平台需要 libsdl2-dev（PC 模拟器显示后端）")
    endif()
endif()
