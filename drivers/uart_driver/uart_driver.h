#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UART 接收数据回调函数类型
 *
 * 业务层通过 uart_driver_start() 注册；驱动接收任务收到数据后调用该回调。
 * 回调在接收任务上下文中执行，应尽快返回，避免阻塞接收。
 *
 * @param data 接收到的数据缓冲区指针，仅在回调返回前有效
 * @param len 接收到的数据长度（字节）
 * @param arg 注册回调时传入的用户参数
 * @return None
 */
typedef void (*uart_driver_rx_cb_t)(const uint8_t *data, size_t len, void *arg);

/**
 * @brief 启动 UART 驱动
 *
 * 初始化 UART 外设（UART_NUM_1，避开被控制台占用的 UART0），
 * 配置 RX/TX 引脚、波特率等参数，安装驱动并创建接收任务。
 * 若未启用 CONFIG_UART_DRIVER_ENABLED，内部会直接跳过，不执行任何操作。
 *
 * @param rx_cb 接收数据回调函数指针（可为 NULL，收到数据仅丢弃）
 * @param rx_arg 回调的用户参数（可为 NULL）
 * @return None
 */
void uart_driver_start(uart_driver_rx_cb_t rx_cb, void *rx_arg);

/**
 * @brief 通过 UART 发送数据
 *
 * 将指定长度的数据通过 UART（UART_NUM_1）发送出去。
 * 调用前需先通过 uart_driver_start() 完成驱动初始化。
 *
 * @param data 待发送的数据缓冲区指针
 * @param len 待发送的数据长度（字节）
 * @return 实际发送的字节数；发送失败时返回 -1
 */
int uart_driver_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
