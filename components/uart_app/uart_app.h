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

#ifdef __cplusplus
}
#endif
