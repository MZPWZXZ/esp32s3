/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* ESP32-S3 应用层（业务逻辑）
 *
 * 本文件只包含业务逻辑，硬件操作统一通过 mqtt_driver / uart_driver 驱动组件完成：
 * - UART 业务：注册接收回调打印数据；定时任务每 1s 发送 0xAA
 * - MQTT 业务：暂未启用（取消注释 mqtt_driver_start 并实现事件回调即可开启）
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "protocol_examples_common.h"

#include "mqtt_driver.h"
#include "uart_driver.h"

static const char *TAG = "esp32s3_app";

/** @brief UART 定时发送间隔（毫秒） */
#define UART_SEND_INTERVAL_MS 1000

/**
 * @brief UART 接收回调（业务逻辑）
 *
 * 由 uart_driver 接收任务调用，打印收到的数据。
 *
 * @param data 接收到的数据缓冲区指针
 * @param len 接收到的数据长度（字节）
 * @param arg 用户参数（本应用未使用，为 NULL）
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
 * @param arg 创建任务时传入的用户参数（本应用未使用，为 NULL）
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

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ===== UART 业务：启动驱动（GPIO1=RX, GPIO2=TX），不依赖网络 ===== */
    uart_driver_start(uart_rx_handler, NULL);
    uart_driver_send((const uint8_t *)"Hello from ESP32-S3!\r\n", sizeof("Hello from ESP32-S3!\r\n") - 1);
    xTaskCreate(uart_periodic_send_task, "uart_periodic_send", 2048, NULL, 10, NULL);

    /* 连接 Wi-Fi：失败仅告警，不崩溃重启，不影响外设继续运行 */
    esp_err_t err = example_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect failed (%s), continuing without network", esp_err_to_name(err));
    }

    /* ===== MQTT 业务（暂未启用） ===== */
    /* 启用方式：取消下面注释，并实现 mqtt_driver_event_cb_t 事件回调
     *（例如在 MQTT_EVENT_CONNECTED 中调用 mqtt_driver_subscribe/publish） */
    // mqtt_driver_start(mqtt_evt_handler, NULL);
}
