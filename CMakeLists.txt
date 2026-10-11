cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)
project(regalo_nico C CXX ASM)
pico_sdk_init()

if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/src/wifi_secrets.h)
    message(FATAL_ERROR "Falta src/wifi_secrets.h: copia src/wifi_secrets.example.h y pon tus credenciales")
endif()

add_executable(regalo_nico
    src/main.c
    src/leds.c
    src/motor.c
    src/wifi.c
    src/web_server.c
    src/control.c
    src/buttons.c
    src/settings.c
    src/mode_switch.c
    lib/dhcpserver/dhcpserver.c
    lib/dnsserver/dnsserver.c
)

# La web se incrusta en el firmware como array C
set(WEB_INDEX ${CMAKE_CURRENT_LIST_DIR}/web/index.html)
set(WEB_INDEX_H ${CMAKE_CURRENT_BINARY_DIR}/generated/index_html.h)
add_custom_command(
    OUTPUT ${WEB_INDEX_H}
    COMMAND ${CMAKE_COMMAND} -DIN=${WEB_INDEX} -DOUT=${WEB_INDEX_H} -DNAME=index_html
            -P ${CMAKE_CURRENT_LIST_DIR}/cmake/embed_file.cmake
    DEPENDS ${WEB_INDEX} ${CMAKE_CURRENT_LIST_DIR}/cmake/embed_file.cmake
)
target_sources(regalo_nico PRIVATE ${WEB_INDEX_H})
pico_generate_pio_header(regalo_nico ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
target_include_directories(regalo_nico PRIVATE src lib/dhcpserver lib/dnsserver ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(regalo_nico PRIVATE CYW43_HOST_NAME=\"cumplenico\")
target_link_libraries(regalo_nico
    pico_stdlib
    hardware_pio
    hardware_pwm
    hardware_flash
    pico_flash
    pico_cyw43_arch_lwip_threadsafe_background
)
pico_enable_stdio_usb(regalo_nico 1)
pico_enable_stdio_uart(regalo_nico 0)
pico_add_extra_outputs(regalo_nico)
