idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_event esp_http_server esp_netif esp_timer esp_wifi json log
             lnot_common mbedtls nvs_flash spi_flash
)
