/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* ESP32-S3 应用入口
 *
 * 本文件只负责系统初始化与业务任务的编排：
 * - 业务逻辑位于 app/task 组件（由任务管理器 task_start_all() 统一启动）
 * - 硬件操作位于 drivers 组件（uart_driver / mqtt_driver）
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

#include "protocol_examples_common.h"

#include "task.h"

static const char *TAG = "esp32s3_app";

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ===== 业务任务：启动不依赖网络的任务（UART 上电即运行） ===== */
    task_start_all();

    /* 连接 Wi-Fi：失败仅告警，不崩溃重启，不影响外设继续运行 */
    esp_err_t err = example_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect failed (%s), continuing without network", esp_err_to_name(err));
    }

    /* ===== 业务任务：MQTT（依赖网络，需在 Wi-Fi 就绪后启动，内部先同步 SNTP 时间） ===== */
    mqtt_task_start();
}
