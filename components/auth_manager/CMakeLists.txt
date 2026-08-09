idf_component_register(
    SRCS "auth_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store json mbedtls
    PRIV_REQUIRES nvs_flash
)
