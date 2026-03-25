cmake_minimum_required(VERSION 3.12)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(aquagate C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.2.0")
    message(FATAL_ERROR "Pico SDK >= 2.2.0 required, got ${PICO_SDK_VERSION_STRING}")
endif()

if (NOT DEFINED PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS)
    set(PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 3000)
endif()

pico_sdk_init()

add_executable(aquagate
    main.cpp
    driver/net/modem/modem.cpp
    driver/net/mqtt/mqtt_client.cpp
    driver/actuators/actuators.c
    driver/storage/storage.c
    lib/command_parser.c
)

target_include_directories(aquagate PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/driver
    ${CMAKE_CURRENT_LIST_DIR}/driver/net
    ${CMAKE_CURRENT_LIST_DIR}/driver/actuators
    ${CMAKE_CURRENT_LIST_DIR}/driver/storage
    ${CMAKE_CURRENT_LIST_DIR}/lib/
)

target_link_libraries(aquagate
    pico_stdlib
    hardware_uart
    hardware_gpio
    hardware_flash
    hardware_sync
)

pico_enable_stdio_usb(aquagate 1)
pico_enable_stdio_uart(aquagate 0)

pico_add_extra_outputs(aquagate)