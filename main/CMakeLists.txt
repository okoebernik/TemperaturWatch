idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES board_pins net_manager config_store rest_api sensor_manager io_manager mqtt_manager snmp_agent auth_manager time_manager
    PRIV_REQUIRES spi_flash app_update
)

spiffs_create_partition_image(www ${CMAKE_SOURCE_DIR}/web FLASH_IN_PROJECT)
