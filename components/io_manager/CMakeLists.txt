idf_component_register(
    SRCS "io_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins config_store gpio_registry sensor_manager json esp_driver_gpio
    PRIV_REQUIRES nvs_flash
)
