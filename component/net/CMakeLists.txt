# 网络组件：ws_client / xz_protocol / http_client
# 组件间不互相 target_link_libraries（audio_* 符号由最终 xiaozhi 链接解析）；
# 单独抽去别的构建时需带走链接契约（见顶层 CMakeLists 链接序注释）。
file(GLOB XZ_NET_SRC ${CMAKE_CURRENT_LIST_DIR}/*.c)
add_library(xz_net STATIC ${XZ_NET_SRC})
# PUBLIC xz_osal：本组件源码用 osal_*，include 需求向上传播
target_link_libraries(xz_net
    PUBLIC  xz_osal
    PRIVATE xz_platform_headers)                     # openssl 头
target_include_directories(xz_net PRIVATE
    ${CMAKE_SOURCE_DIR}/app                          # "app_config.h"
    ${CMAKE_SOURCE_DIR}/component/audio              # xz_protocol.c 的 "audio_cap.h"/"audio_play.h"/"opus_codec.h"
)
