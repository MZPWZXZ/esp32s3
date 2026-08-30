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
#include <string.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include "mqtt_driver.h"
#include "uart_driver.h"
#include "task.h"

static const char *TAG = "task";

/** @brief UART 定时发送间隔（毫秒） */
#define UART_SEND_INTERVAL_MS 1000

#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
/** @brief OneNET 主题前缀：$sys/{产品ID}/{设备名称} */
#define ONENET_TOPIC_BASE "$sys/" CONFIG_MQTT_DRIVER_PRODUCT_ID "/" CONFIG_MQTT_DRIVER_DEVICE_NAME
/** @brief 命令下发主题前缀（+ 匹配 cmdid） */
#define ONENET_CMD_REQUEST_PREFIX  ONENET_TOPIC_BASE "/cmd/request/"
/** @brief 命令回复主题前缀（需拼接 cmdid） */
#define ONENET_CMD_RESPONSE_PREFIX ONENET_TOPIC_BASE "/cmd/response/"
#endif

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
#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
        /* OneNET：订阅数据上报响应主题 + 命令下发主题 */
        mqtt_driver_subscribe(ONENET_TOPIC_BASE "/dp/post/json/+", 0);
        mqtt_driver_subscribe(ONENET_CMD_REQUEST_PREFIX "+", 0);
#else
        mqtt_driver_subscribe("topic/qos0", 0);
        mqtt_driver_subscribe("topic/qos1", 1);
#endif
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
        /* OneNET：上报一个数据点（JSON 格式） */
        mqtt_driver_publish(ONENET_TOPIC_BASE "/dp/post/json",
                            "{\"datastreams\":[{\"id\":\"temp\",\"datapoints\":[{\"value\":25}]}]}", -1, 0, 0);
#else
        mqtt_driver_publish("topic/qos0", "data", 4, 0, 0);
#endif
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data: topic=%.*s data=%.*s",
                 event->topic_len, event->topic, event->data_len, event->data);
#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
        /* OneNET 命令下发：$sys/{pid}/{dev}/cmd/request/{cmdid}
         * 收到命令后回复到 $sys/{pid}/{dev}/cmd/response/{cmdid} */
        {
            char topic_buf[128];
            int tlen = event->topic_len < (int)sizeof(topic_buf) - 1 ? event->topic_len : (int)sizeof(topic_buf) - 1;
            memcpy(topic_buf, event->topic, (size_t)tlen);
            topic_buf[tlen] = '\0';
            const char *cmdid = strrchr(topic_buf, '/');
            if (cmdid != NULL && strncmp(topic_buf, ONENET_CMD_REQUEST_PREFIX, strlen(ONENET_CMD_REQUEST_PREFIX)) == 0) {
                cmdid++; /* 跳过 '/'，得到 cmdid */
                char resp_topic[160];
                snprintf(resp_topic, sizeof(resp_topic), ONENET_CMD_RESPONSE_PREFIX "%s", cmdid);
                const char *resp = "{\"code\":0,\"msg\":\"ok\"}";
                mqtt_driver_publish(resp_topic, resp, -1, 0, 0);
                ESP_LOGI(TAG, "Command received, replied on %s", resp_topic);
            }
        }
#endif
        break;
    default:
        break;
    }
}

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
 * 先同步系统时间（OneNET token 需要），再启动 MQTT 驱动。
 *
 * @param None
 * @return None
 */
void mqtt_task_start(void)
{
    sync_system_time();
    mqtt_driver_start(mqtt_evt_handler, NULL);
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
