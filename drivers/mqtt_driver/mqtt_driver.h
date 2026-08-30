#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 MQTT 驱动
 *
 * 初始化 MQTT 客户端并连接配置的 Broker（消息服务器）。
 * 驱动内部注册事件处理回调：连接成功后自动订阅主题，
 * 启用 OneNET 认证时按 OneNET 协议处理订阅/上报/命令下发回复。
 * 若未启用 CONFIG_MQTT_DRIVER_ENABLED，内部会直接跳过，不执行任何操作。
 *
 * @param None
 * @return None
 */
void mqtt_driver_start(void);

/**
 * @brief 发布消息到指定主题
 *
 * @param topic 目标主题（C 字符串）
 * @param data 消息数据（C 字符串，也可为任意字节流）
 * @param len 数据长度（字节）
 * @param qos QoS 等级（0/1/2）
 * @param retain 是否保留消息
 * @return 消息 ID；驱动未启动或发送失败时返回 -1
 */
int mqtt_driver_publish(const char *topic, const char *data, int len, int qos, int retain);

/**
 * @brief 订阅主题
 *
 * @param topic 要订阅的主题（C 字符串）
 * @param qos QoS 等级（0/1/2）
 * @return 消息 ID；驱动未启动或订阅失败时返回 -1
 */
int mqtt_driver_subscribe(const char *topic, int qos);

/**
 * @brief 取消订阅主题
 *
 * @param topic 要取消订阅的主题（C 字符串）
 * @return 消息 ID；驱动未启动或取消订阅失败时返回 -1
 */
int mqtt_driver_unsubscribe(const char *topic);

#ifdef __cplusplus
}
#endif
