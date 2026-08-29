#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 MQTT 客户端。若未启用 CONFIG_MQTT_APP_ENABLED，内部会直接跳过。 */
void mqtt_app_start(void);

#ifdef __cplusplus
}
#endif
