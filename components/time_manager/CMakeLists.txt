idf_component_register(
    SRCS "time_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store json
    PRIV_REQUIRES esp_netif lwip nvs_flash
)
