idf_component_register(
    SRCS "task.c"
    PRIV_REQUIRES mqtt_driver uart_driver esp_netif freertos
    INCLUDE_DIRS "."
)
