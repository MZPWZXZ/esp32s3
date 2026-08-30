#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 UART 通信
 *
 * 初始化 UART 外设（使用 UART_NUM_1，避开被控制台占用的 UART0），
 * 配置 RX/TX 引脚、波特率等参数，安装驱动并创建接收任务。
 * 若未启用 CONFIG_UART_APP_ENABLED，内部会直接跳过，不执行任何操作。
 *
 * @param None
 * @return None
 */
void uart_app_start(void);

/**
 * @brief 通过 UART 发送数据
 *
 * 将指定长度的数据通过 UART（UART_NUM_1）发送出去。
 * 调用前需先通过 uart_app_start() 完成驱动初始化。
 *
 * @param data 待发送的数据缓冲区指针
 * @param len 待发送的数据长度（字节）
 * @return 实际发送的字节数；发送失败时返回 -1
 */
int uart_app_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
