# OSAL：纯 C99 接口头(include/) + POSIX 参考实现(posix/)；MCU 移植换 posix/ 目录
file(GLOB XZ_OSAL_SRC ${CMAKE_CURRENT_LIST_DIR}/posix/*.c)
add_library(xz_osal STATIC ${XZ_OSAL_SRC})
target_include_directories(xz_osal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(xz_osal PRIVATE xz_platform_headers)   # osal_tls.c 要 <openssl/ssl.h>
