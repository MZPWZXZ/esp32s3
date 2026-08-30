#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT 事件回调函数类型
 *
 * 业务层通过 mqtt_driver_start() 注册；驱动收到 MQTT 事件后分发到该回调。
 *
 * @param event_id 事件 ID（esp_mqtt_event_id_t 枚举值）
 * @param event_data 事件数据，类型为 esp_mqtt_event_handle_t，仅在回调返回前有效
 * @param arg 注册回调时传入的用户参数
 * @return None
 */
typedef void (*mqtt_driver_event_cb_t)(int32_t event_id, void *event_data, void *arg);

/**
 * @brief 启动 MQTT 驱动
 *
 * 初始化 MQTT 客户端并连接配置的 Broker（消息服务器）。
 * 驱动内部将收到的 MQTT 事件分发到 evt_cb 回调，不包含任何业务逻辑。
 * 若未启用 CONFIG_MQTT_DRIVER_ENABLED，内部会直接跳过，不执行任何操作。
 *
 * @param evt_cb 事件回调函数指针（可为 NULL，仅记录日志不回调）
 * @param evt_arg 回调的用户参数（可为 NULL）
 * @return None
 */
void mqtt_driver_start(mqtt_driver_event_cb_t evt_cb, void *evt_arg);

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
