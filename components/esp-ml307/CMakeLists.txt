# Common source files for all targets
set(COMMON_SRCS
    "src/network_error.cc"
    "src/esp/esp_network.cc"
    "src/esp/esp_ssl.cc"
    "src/esp/esp_tcp.cc"
    "src/esp/esp_mqtt.cc"
    "src/esp/esp_udp.cc"
    "src/web_socket.cc"
    "src/http_client.cc"
)

# Cellular modem sources.
#
# Upstream gates these on "NOT esp32", which would also enable them for the
# ESP32-S2. The S2 has no UHCI DMA controller, so at_uart.h cannot even include
# <uart_uhci.h>, and the 78/uart-uhci component aborts configuration with
# "unable to locate hal/uhci_ll.h for esp32s2". Only build the modem path on the
# targets that actually have UHCI.
if(IDF_TARGET STREQUAL "esp32s3" OR IDF_TARGET STREQUAL "esp32c3")
    list(APPEND COMMON_SRCS
        "src/at_uart.cc"
        "src/at_modem.cc"
        "src/ec801e/ec801e_at_modem.cc"
        "src/ec801e/ec801e_tcp.cc"
        "src/ec801e/ec801e_ssl.cc"
        "src/ec801e/ec801e_udp.cc"
        "src/ec801e/ec801e_mqtt.cc"
        "src/ml307/ml307_at_modem.cc"
        "src/ml307/ml307_tcp.cc"
        "src/ml307/ml307_ssl.cc"
        "src/ml307/ml307_mqtt.cc"
        "src/ml307/ml307_udp.cc"
        "src/ml307/ml307_http.cc"
    )
endif()

idf_component_register(
    SRCS
        ${COMMON_SRCS}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "."
    REQUIRES
        "esp_driver_gpio"
        "esp_driver_uart"
        "esp_pm"
        "esp-tls"
        "pthread"
        "mqtt"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
