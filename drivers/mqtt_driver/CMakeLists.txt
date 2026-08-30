idf_component_register(
    SRCS "mqtt_driver.c"
    PRIV_REQUIRES esp-tls mbedtls
    INCLUDE_DIRS "."
    EMBED_TXTFILES "mosquitto.org.crt"
)
