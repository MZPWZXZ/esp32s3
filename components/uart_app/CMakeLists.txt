idf_component_register(
    SRCS "uart_app.c"
    PRIV_REQUIRES esp_driver_uart freertos
    INCLUDE_DIRS "."
)
