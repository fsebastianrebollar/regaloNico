cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

# Mbed TLS (HTTPS para buscar actualizaciones en GitHub). Si el submódulo del
# SDK no está descargado, se baja la misma versión dentro de build/.
set(MBEDTLS_COMMIT 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20)
if(NOT PICO_MBEDTLS_PATH AND NOT EXISTS ${PICO_SDK_PATH}/lib/mbedtls/include/mbedtls)
    set(PICO_MBEDTLS_PATH ${CMAKE_BINARY_DIR}/_deps/mbedtls-${MBEDTLS_COMMIT})
    if(NOT EXISTS ${PICO_MBEDTLS_PATH}/include/mbedtls)
        message(STATUS "Descargando Mbed TLS ${MBEDTLS_COMMIT}")
        file(DOWNLOAD https://github.com/Mbed-TLS/mbedtls/archive/${MBEDTLS_COMMIT}.tar.gz
             ${CMAKE_BINARY_DIR}/_deps/mbedtls.tar.gz STATUS dl_status)
        list(GET dl_status 0 dl_code)
        if(NOT dl_code EQUAL 0)
            message(FATAL_ERROR "No se pudo descargar Mbed TLS: ${dl_status}")
        endif()
        file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/_deps/mbedtls.tar.gz DESTINATION ${CMAKE_BINARY_DIR}/_deps)
    endif()
endif()

# Versión del firmware: la usa la búsqueda de actualizaciones y el arranque A/B
file(READ ${CMAKE_CURRENT_LIST_DIR}/VERSION FW_VERSION)
string(STRIP "${FW_VERSION}" FW_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/VERSION)
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
    src/ota.c
    src/https_get.c
    src/alexa.c
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
target_compile_definitions(regalo_nico PRIVATE
    CYW43_HOST_NAME=\"cumplenico\"
    FW_VERSION=${FW_VERSION}
    # Prueba antes de quedarse: si la versión nueva no arranca bien, vuelve a la anterior
    PICO_CRT0_IMAGE_TYPE_TBYB=1
    # El token de GitHub viaja por aquí: el certificado tiene que ser válido
    ALTCP_MBEDTLS_AUTHMODE=MBEDTLS_SSL_VERIFY_REQUIRED
)
pico_set_binary_version(regalo_nico MAJOR ${FW_VERSION})
pico_hash_binary(regalo_nico)
target_link_libraries(regalo_nico
    pico_stdlib
    hardware_pio
    hardware_pwm
    hardware_flash
    pico_flash
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mbedtls
    pico_mbedtls
    boot_uf2_headers
    boot_picoboot_headers
)
set_source_files_properties(${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-result")
pico_enable_stdio_usb(regalo_nico 1)
pico_enable_stdio_uart(regalo_nico 0)
pico_add_extra_outputs(regalo_nico)
