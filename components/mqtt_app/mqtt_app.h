#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 MQTT 客户端
 *
 * 初始化并启动 MQTT 客户端，连接配置的 Broker（消息服务器）。
 * 若未启用 CONFIG_MQTT_APP_ENABLED，内部会直接跳过，不执行任何操作。
 *
 * @param None
 * @return None
 */
void mqtt_app_start(void);

#ifdef __cplusplus
}
#endif
