set(TUSB "${CMAKE_CURRENT_LIST_DIR}/../../../../external/tinyusb/src")
idf_component_register(
  SRCS "${TUSB}/tusb.c" "${TUSB}/common/tusb_fifo.c"
    "${TUSB}/device/usbd.c" "${TUSB}/device/usbd_control.c"
    "${TUSB}/portable/synopsys/dwc2/dcd_dwc2.c"
    "${TUSB}/portable/synopsys/dwc2/dwc2_common.c"
  INCLUDE_DIRS "${TUSB}" "${CMAKE_CURRENT_LIST_DIR}/../.."
  REQUIRES freertos esp_hw_support esp_mm esp_timer)
