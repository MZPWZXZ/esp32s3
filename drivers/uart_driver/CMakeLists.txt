idf_component_register(
    SRCS "uart_driver.c"
    PRIV_REQUIRES esp_driver_uart freertos
    INCLUDE_DIRS "."
)
