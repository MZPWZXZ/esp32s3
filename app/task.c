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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

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
 * @brief MQTT 事件回调（业务逻辑）
 *
 * 由 mqtt_driver 分发事件到此回调，处理订阅/发布/收数等业务。
 *
 * @param event_id 事件 ID（esp_mqtt_event_id_t）
 * @param event_data 事件数据，类型为 esp_mqtt_event_handle_t，仅在回调返回前有效
 * @param arg 用户参数（本任务未使用，为 NULL）
 * @return None
 */
static void mqtt_evt_handler(int32_t event_id, void *event_data, void *arg)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected, subscribing topics");
        mqtt_driver_subscribe("topic/qos0", 0);
        mqtt_driver_subscribe("topic/qos1", 1);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
        mqtt_driver_publish("topic/qos0", "data", 4, 0, 0);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data: topic=%.*s data=%.*s",
                 event->topic_len, event->topic, event->data_len, event->data);
        break;
    default:
        break;
    }
}

/**
 * @brief 启动 MQTT 业务任务
 *
 * @param None
 * @return None
 */
void mqtt_task_start(void)
{
    mqtt_driver_start(mqtt_evt_handler, NULL);
}

/* ============================ 统一入口 ============================ */

/**
 * @brief 启动全部业务任务
 *
 * @param None
 * @return None
 */
void task_start_all(void)
{
    ESP_LOGI(TAG, "Starting all business tasks");

    /* UART 业务任务：不依赖网络，上电即运行（GPIO1=RX, GPIO2=TX） */
    uart_task_start();

    /* MQTT 业务任务：连接配置的 Broker（需要网络，Wi-Fi 连不上时自动重试） */
    mqtt_task_start();
}
