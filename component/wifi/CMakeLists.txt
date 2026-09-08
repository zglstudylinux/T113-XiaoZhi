# WiFi 组件：wifi_manager（自实现 wpa_supplicant ctrl 协议，无外部库依赖）
file(GLOB XZ_WIFI_SRC ${CMAKE_CURRENT_LIST_DIR}/*.c)
add_library(xz_wifi STATIC ${XZ_WIFI_SRC})
target_link_libraries(xz_wifi PUBLIC xz_osal)
