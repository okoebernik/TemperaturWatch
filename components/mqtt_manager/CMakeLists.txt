idf_component_register(
    SRCS "mqtt_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store sensor_manager io_manager mqtt json esp_timer
    PRIV_REQUIRES nvs_flash
)
