idf_component_register(
    SRCS "task/task.c"
    PRIV_REQUIRES mqtt_driver uart_driver espressif__mqtt freertos
    INCLUDE_DIRS "." "task"
)
