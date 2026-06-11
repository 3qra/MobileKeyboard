cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Target board")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(mobile_keyboard C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(MOBILEKBD_WIFI_SSID "PicoKeyboard" CACHE STRING "Wi-Fi access point SSID")
set(MOBILEKBD_WIFI_PASSWORD "changeme0" CACHE STRING "Wi-Fi access point password; leave empty for open AP")
set(MOBILEKBD_UDP_PORT "4242" CACHE STRING "UDP listen port")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mobile_keyboard.local.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/mobile_keyboard.local.cmake")
endif()

add_executable(mobile_keyboard
    src/dhcp_server.cpp
    src/http_server.cpp
    src/main.cpp
    src/usb_descriptors.cpp
)

target_include_directories(mobile_keyboard PRIVATE
    include
)

target_compile_definitions(mobile_keyboard PRIVATE
    WIFI_SSID="${MOBILEKBD_WIFI_SSID}"
    WIFI_PASSWORD="${MOBILEKBD_WIFI_PASSWORD}"
    UDP_LISTEN_PORT=${MOBILEKBD_UDP_PORT}
)

target_link_libraries(mobile_keyboard PRIVATE
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_uart(mobile_keyboard 1)
pico_enable_stdio_usb(mobile_keyboard 0)

pico_add_extra_outputs(mobile_keyboard)
