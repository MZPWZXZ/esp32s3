/**
 * @file uart_driver.c
 * @brief UART 驱动实现
 *
 * 本文件仅实现 UART 驱动能力：外设初始化、数据发送，
 * 以及接收数据到回调的分发（不包含任何业务逻辑）。
 * 默认使用 GPIO1 作为 RX、GPIO2 作为 TX（可通过 menuconfig 修改）。
 *
 * @author esp32s3 工程
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/uart.h"

#include "uart_driver.h"

static const char *TAG = "uart_driver";

/** @brief 接收缓冲区大小（字节） */
#define UART_DRIVER_RX_BUF_SIZE 1024

/** @brief 业务层注册的接收回调及其参数 */
static uart_driver_rx_cb_t s_rx_cb = NULL;
static void *s_rx_arg = NULL;

/**
 * @brief UART 接收数据处理任务（驱动内部）
 *
 * 循环读取 UART 接收到的数据，并分发到业务层注册的回调 s_rx_cb；
 * 未注册回调时仅丢弃数据，不执行任何业务处理。
 *
 * @param arg 创建任务时传入的用户参数（本驱动中不使用，为 NULL）
 * @return None
 */
static void uart_rx_task(void *arg)
{
    uint8_t data[UART_DRIVER_RX_BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, data, sizeof(data), pdMS_TO_TICKS(100));
        if (len > 0) {
            if (s_rx_cb != NULL) {
                s_rx_cb(data, (size_t)len, s_rx_arg);
            } else {
                ESP_LOGD(TAG, "Received %d bytes, no callback registered", len);
            }
        }
    }
}

/**
 * @brief 启动 UART 驱动
 *
 * 根据 menuconfig 中的配置决定是否启动 UART：
 * - 启用 CONFIG_UART_DRIVER_ENABLED 时：配置波特率、数据位/校验/停止位，
 *   将 RX/TX 引脚映射到 UART_NUM_1，安装驱动并创建接收任务。
 * - 未启用时：打印警告日志并直接返回。
 *
 * @param rx_cb 接收数据回调函数指针（可为 NULL）
 * @param rx_arg 回调的用户参数（可为 NULL）
 * @return None
 */
void uart_driver_start(uart_driver_rx_cb_t rx_cb, void *rx_arg)
{
#if CONFIG_UART_DRIVER_ENABLED
    s_rx_cb = rx_cb;
    s_rx_arg = rx_arg;

    /* 配置 UART 参数：波特率取自 menuconfig，8 数据位，无校验，1 停止位 */
    uart_config_t uart_config = {
        .baud_rate = CONFIG_UART_DRIVER_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));

    /* 设置引脚：CONFIG_UART_DRIVER_TX_GPIO（默认 GPIO2）为 TX，
     * CONFIG_UART_DRIVER_RX_GPIO（默认 GPIO1）为 RX */
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, CONFIG_UART_DRIVER_TX_GPIO, CONFIG_UART_DRIVER_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /* 安装 UART 驱动，并分配接收缓冲区（发送缓冲区为 0，使用阻塞写入） */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, UART_DRIVER_RX_BUF_SIZE * 2, 0, 0, NULL, 0));

    ESP_LOGI(TAG, "UART started: TX=GPIO%d, RX=GPIO%d, baud=%d",
             CONFIG_UART_DRIVER_TX_GPIO, CONFIG_UART_DRIVER_RX_GPIO, CONFIG_UART_DRIVER_BAUD_RATE);

    /* 创建接收任务 */
    xTaskCreate(uart_rx_task, "uart_rx_task", 2048, NULL, 10, NULL);
#else
    ESP_LOGW(TAG, "UART disabled: CONFIG_UART_DRIVER_ENABLED is not set");
#endif
}

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
int uart_driver_send(const uint8_t *data, size_t len)
{
    return uart_write_bytes(UART_NUM_1, data, len);
}
