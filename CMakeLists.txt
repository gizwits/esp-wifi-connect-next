set(SOURCES
    "wifi_configuration_ap.cc"
    "wifi_station.cc"
    "ssid_manager.cc"
    "wifi_manager_c.cc"
    "wifi_connection_manager.cc"
    "dns_server.cc"
    "wifi_configuration.cc"
    "protocol/parse_protocol.c"
    "protocol/pack_protocol.c"
)

set(INCLUDE_DIRS
    "include"
)

set(REQUIRES
    "esp_timer"
    "esp_http_server"
    "esp_wifi"
    "nvs_flash"
    "json"
)

if(CONFIG_BT_NIMBLE_ENABLED)
    list(APPEND SOURCES
        "wifi_configuration_ble.cc"
        "ble/adv.c"
        "ble/ble.c"
        "ble/gatt_svr.c"
        "ble/misc.c"
    )
    list(APPEND INCLUDE_DIRS "include_ble")
    list(APPEND REQUIRES "bt")
endif()

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    EMBED_TXTFILES
        "assets/wifi_configuration.html"
        "assets/wifi_configuration_done.html"
    REQUIRES ${REQUIRES}
)

