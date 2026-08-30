/**
 * @file task.c
 * @brief 任务管理器实现（业务层）
 *
 * 本文件属于业务层（app），统一实现与管理所有业务任务：
 * - UART 任务：接收回调打印数据 + 定时发送 0xAA
 * - MQTT 任务：连接 Broker 后订阅/发布（默认未启用）
 *
 * 本文件只编排业务逻辑，不直接操作硬件/网络，
 * 底层操作通过 mqtt_driver / uart_driver 驱动组件完成。
 *
 * @author esp32s3 工程
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mqtt_driver.h"
#include "uart_driver.h"
#include "task.h"

static const char *TAG = "task";

/** @brief UART 定时发送间隔（毫秒） */
#define UART_SEND_INTERVAL_MS 1000

/* ============================ UART 任务 ============================ */

/**
 * @brief UART 接收回调（业务逻辑）
 *
 * 由 uart_driver 接收任务调用，打印收到的数据。
 *
 * @param data 接收到的数据缓冲区指针
 * @param len 接收到的数据长度（字节）
 * @param arg 用户参数（本任务未使用，为 NULL）
 * @return None
 */
static void uart_rx_handler(const uint8_t *data, size_t len, void *arg)
{
    ESP_LOGI(TAG, "UART received %d bytes: %.*s", (int)len, (int)len, (const char *)data);
}

/**
 * @brief UART 定时发送任务（业务逻辑）
 *
 * 每 UART_SEND_INTERVAL_MS ms 通过 uart_driver 发送一个字节 0xAA。
 *
 * @param arg 创建任务时传入的用户参数（本任务未使用，为 NULL）
 * @return None
 */
static void uart_periodic_send_task(void *arg)
{
    const uint8_t tx_byte = 0xAA;
    while (1) {
        uart_driver_send(&tx_byte, sizeof(tx_byte));
        vTaskDelay(pdMS_TO_TICKS(UART_SEND_INTERVAL_MS));
    }
}

/**
 * @brief 启动 UART 业务任务
 *
 * @param None
 * @return None
 */
void uart_task_start(void)
{
    /* 启动 UART 驱动并注册接收回调（GPIO1=RX, GPIO2=TX） */
    uart_driver_start(uart_rx_handler, NULL);
    /* 发送一条启动提示消息 */
    uart_driver_send((const uint8_t *)"Hello from ESP32-S3!\r\n", sizeof("Hello from ESP32-S3!\r\n") - 1);
    /* 创建定时发送任务 */
    xTaskCreate(uart_periodic_send_task, "uart_periodic_send", 2048, NULL, 10, NULL);
}

/* ============================ MQTT 任务 ============================ */

/**
 * @brief 通过 SNTP 同步系统时间
 *
 * OneNET token 的过期时间基于当前时间计算，
 * 时间未同步（1970 起点）会导致生成的 token 被服务器拒绝。
 *
 * @param None
 * @return None
 */
static void sync_system_time(void)
{
    ESP_LOGI(TAG, "Syncing system time via SNTP...");
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
    esp_netif_sntp_start();
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK) {
        ESP_LOGI(TAG, "System time synced");
    } else {
        ESP_LOGW(TAG, "SNTP sync timeout, OneNET token may be rejected");
    }
}

/**
 * @brief 启动 MQTT 业务任务
 *
 * 先同步系统时间（OneNET token 需要），再启动 MQTT 驱动
 * （驱动内部自动处理订阅、上报与命令回复）。
 *
 * @param None
 * @return None
 */
void mqtt_task_start(void)
{
    sync_system_time();
    mqtt_driver_start();
}

/* ============================ 统一入口 ============================ */

/**
 * @brief 启动全部业务任务
 *
 * 统一创建并启动不依赖网络的全部业务任务（当前为 UART 任务）。
 * MQTT 任务依赖网络，由应用层在 Wi-Fi 连接就绪后单独调用
 * mqtt_task_start() 启动（内部会先同步 SNTP 时间）。
 *
 * @param None
 * @return None
 */
void task_start_all(void)
{
    ESP_LOGI(TAG, "Starting all business tasks");

    /* UART 业务任务：不依赖网络，上电即运行（GPIO1=RX, GPIO2=TX） */
    uart_task_start();
}
