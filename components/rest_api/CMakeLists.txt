idf_component_register(
    SRCS "rest_api.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server spiffs json board_pins config_store net_manager sensor_manager io_manager mqtt_manager snmp_agent auth_manager time_manager app_update esp_partition esp_timer
)
