set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
idf_component_register(
  SRCS "main.c"
       "platform.c"
       "settings.c"
       "${ROOT}/src/asix_protocol.c"
       "${ROOT}/src/usb_asix.c"
       "${ROOT}/src/services.c"
       "${ROOT}/src/manager.c"
       "${ROOT}/src/console.c"
       "${ROOT}/src/forward.c"
       "${ROOT}/src/settings.c"
       "${ROOT}/generated/payload.c"
  INCLUDE_DIRS "${ROOT}/src" "${ROOT}/generated"
  REQUIRES esp_wifi
           esp_netif
           esp_event
           nvs_flash
           esp_timer
           esp_driver_gpio
           esp_hw_support
           lwip
           mst_tinyusb
           usb)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter
                                                -Wno-misleading-indentation)
