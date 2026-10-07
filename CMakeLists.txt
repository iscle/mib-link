cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD
    pico_w
    CACHE STRING "Board")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(mib_link C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
add_executable(
  mib_link
  platform/pico_w/main.c
  platform/pico_w/platform.c
  platform/pico_w/settings.c
  src/asix_protocol.c
  src/usb_asix.c
  src/services.c
  src/manager.c
  src/settings.c
  src/forward.c
  src/console.c
  vendor/dhcpserver.c
  generated/payload.c)
target_compile_definitions(mib_link PRIVATE PICO_STACK_SIZE=0x1000)
target_include_directories(mib_link PRIVATE src platform/pico_w vendor generated)
target_compile_options(mib_link PRIVATE -fstack-usage -Wall -Wextra -Werror -Wno-unused-parameter
                                        -Wno-misleading-indentation)
target_link_libraries(
  mib_link
  pico_stdlib
  pico_unique_id
  pico_cyw43_arch_lwip_poll
  tinyusb_device
  hardware_flash
  hardware_sync
  hardware_watchdog)
pico_add_linker_script_override_path(mib_link ${CMAKE_CURRENT_LIST_DIR}/linker)
pico_enable_stdio_usb(mib_link 0)
pico_enable_stdio_uart(mib_link 0)
pico_set_program_name(mib_link "MIB-Link")
pico_set_program_version(mib_link "1.2.1")
pico_add_extra_outputs(mib_link)
