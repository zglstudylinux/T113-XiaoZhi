# 音频组件：audio_cap / audio_play / opus_codec / resample
file(GLOB XZ_AUDIO_SRC ${CMAKE_CURRENT_LIST_DIR}/*.c)
add_library(xz_audio STATIC ${XZ_AUDIO_SRC})
target_link_libraries(xz_audio
    PUBLIC  xz_osal
    PRIVATE xz_platform_headers)                     # <alsa/asoundlib.h> <opus/opus.h> <speex/...>
target_include_directories(xz_audio PRIVATE
    ${CMAKE_SOURCE_DIR}/app)                         # audio_play.c 的 "app_config.h"
