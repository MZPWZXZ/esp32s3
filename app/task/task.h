#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动全部业务任务
 *
 * 统一创建并启动工程内的所有业务任务：
 * - UART 任务：接收回调 + 定时发送 0xAA（默认启用）
 * - MQTT 任务：连接 Broker 并订阅/发布（默认未启用）
 *
 * @param None
 * @return None
 */
void task_start_all(void);

/**
 * @brief 启动 UART 业务任务
 *
 * 注册 UART 接收回调，并创建定时发送任务（每 1s 发送一个字节 0xAA）。
 * 底层硬件操作由 uart_driver 组件完成，本模块只编排业务逻辑。
 *
 * @param None
 * @return None
 */
void uart_task_start(void);

/**
 * @brief 启动 MQTT 业务任务
 *
 * 启动 MQTT 驱动并注册事件回调：连接成功后订阅主题，
 * 收到订阅确认后发布消息，收到数据时打印主题与内容。
 * 底层网络操作由 mqtt_driver 组件完成，本模块只编排业务逻辑。
 *
 * @param None
 * @return None
 */
void mqtt_task_start(void);

#ifdef __cplusplus
}
#endif
