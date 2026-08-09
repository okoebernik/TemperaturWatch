idf_component_register(
    SRCS "sensor_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins config_store dallas_sensor dht_sensor gpio_registry json esp_timer
    PRIV_REQUIRES nvs_flash
)
