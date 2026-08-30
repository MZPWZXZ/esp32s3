/**
 * @file uart_app.c
 * @brief UART 通信应用组件实现
 *
 * 本文件实现 UART 外设的初始化、启动以及接收数据处理逻辑，
 * 默认使用 GPIO1 作为 RX、GPIO2 作为 TX（可通过 menuconfig 修改）。
 *
 * @author esp32s3 工程
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/uart.h"

#include "uart_app.h"

static const char *TAG = "uart_app";

/** @brief 接收缓冲区大小（字节） */
#define UART_APP_RX_BUF_SIZE 1024

/**
 * @brief UART 接收数据处理任务
 *
 * 循环读取 UART 接收到的数据并打印到日志。
 * 仅在读取到数据时输出，避免空转刷屏。
 *
 * @param arg 创建任务时传入的用户参数（本组件中不使用，为 NULL）
 * @return None
 */
static void uart_rx_task(void *arg)
{
    uint8_t data[UART_APP_RX_BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, data, sizeof(data), pdMS_TO_TICKS(100));
        if (len > 0) {
            ESP_LOGI(TAG, "Received %d bytes: %.*s", len, len, (char *)data);
        }
    }
}

#if CONFIG_UART_APP_PERIODIC_SEND
/**
 * @brief 定时发送任务
 *
 * 按 CONFIG_UART_APP_SEND_INTERVAL_MS 配置的间隔（默认 1000ms）
 * 周期性地通过 UART 发送一个字节 0xAA。
 *
 * @param arg 创建任务时传入的用户参数（本组件中不使用，为 NULL）
 * @return None
 */
static void uart_tx_task(void *arg)
{
    const uint8_t tx_byte = 0xAA;
    while (1) {
        int written = uart_app_send(&tx_byte, sizeof(tx_byte));
        if (written < 0) {
            ESP_LOGW(TAG, "Periodic send failed");
        } else {
            ESP_LOGD(TAG, "Sent 0xAA (%d byte)", written);
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_UART_APP_SEND_INTERVAL_MS));
    }
}
#endif /* CONFIG_UART_APP_PERIODIC_SEND */

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
int uart_app_send(const uint8_t *data, size_t len)
{
    return uart_write_bytes(UART_NUM_1, data, len);
}

/**
 * @brief 启动 UART 通信
 *
 * 根据 menuconfig 中的配置决定是否启动 UART：
 * - 启用 CONFIG_UART_APP_ENABLED 时：配置波特率、数据位/校验/停止位，
 *   将 RX/TX 引脚映射到 UART_NUM_1，安装驱动并创建接收任务。
 * - 未启用时：打印警告日志并直接返回。
 *
 * @param None
 * @return None
 */
void uart_app_start(void)
{
#if CONFIG_UART_APP_ENABLED
    /* 配置 UART 参数：波特率取自 menuconfig，8 数据位，无校验，1 停止位 */
    uart_config_t uart_config = {
        .baud_rate = CONFIG_UART_APP_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));

    /* 设置引脚：CONFIG_UART_APP_TX_GPIO（默认 GPIO2）为 TX，
     * CONFIG_UART_APP_RX_GPIO（默认 GPIO1）为 RX */
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, CONFIG_UART_APP_TX_GPIO, CONFIG_UART_APP_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /* 安装 UART 驱动，并分配接收缓冲区（发送缓冲区为 0，使用阻塞写入） */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, UART_APP_RX_BUF_SIZE * 2, 0, 0, NULL, 0));

    ESP_LOGI(TAG, "UART started: TX=GPIO%d, RX=GPIO%d, baud=%d",
             CONFIG_UART_APP_TX_GPIO, CONFIG_UART_APP_RX_GPIO, CONFIG_UART_APP_BAUD_RATE);

    /* 发送一条启动提示消息 */
    const char *hello = "Hello from ESP32-S3 UART app!\r\n";
    uart_write_bytes(UART_NUM_1, hello, strlen(hello));

    /* 创建接收任务 */
    xTaskCreate(uart_rx_task, "uart_rx_task", 2048, NULL, 10, NULL);
#if CONFIG_UART_APP_PERIODIC_SEND
    /* 创建定时发送任务（每 CONFIG_UART_APP_SEND_INTERVAL_MS ms 发送一次 0xAA） */
    xTaskCreate(uart_tx_task, "uart_tx_task", 2048, NULL, 10, NULL);
#endif
#else
    ESP_LOGW(TAG, "UART disabled: CONFIG_UART_APP_ENABLED is not set");
#endif
}
