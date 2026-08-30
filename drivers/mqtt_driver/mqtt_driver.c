/**
 * @file mqtt_driver.c
 * @brief MQTT 客户端驱动实现
 *
 * 本文件实现 MQTT 驱动能力：客户端初始化、启动、
 * 发布/订阅/取消订阅、事件处理（连接后订阅主题、数据上报、
 * 命令下发回复），以及 OneNET 平台的 token 生成与 CONNECT 认证。
 *
 * @author esp32s3 工程
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "mbedtls/base64.h"
#include "mbedtls/md.h"

#include "mqtt_driver.h"

static const char *TAG = "mqtt_driver";

/** @brief MQTT 客户端句柄（驱动内部持有，供发布/订阅使用） */
static esp_mqtt_client_handle_t s_client = NULL;

#if CONFIG_MQTT_DRIVER_BROKER_CERTIFICATE_OVERRIDDEN
static const char cert_override_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    CONFIG_MQTT_DRIVER_BROKER_CERTIFICATE_OVERRIDE "\n"
    "-----END CERTIFICATE-----";
#endif

#if CONFIG_MQTT_DRIVER_CERT_VALIDATE_MOSQUITTO_CA
/* 内嵌的 Mosquitto CA 证书，用于 test.mosquitto.org:8883 的服务器证书校验 */
extern const uint8_t mosquitto_org_crt_start[] asm("_binary_mosquitto_org_crt_start");
extern const uint8_t mosquitto_org_crt_end[] asm("_binary_mosquitto_org_crt_end");
#endif

#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
/** @brief OneNET token 算法参数（协议版本 2018-10-31） */
#define ONENET_TOKEN_VERSION "2018-10-31"
#define ONENET_TOKEN_METHOD  "sha1"

/** @brief OneNET Studio 物模型主题 */
#define ONENET_TOPIC_BASE               "$sys/" CONFIG_MQTT_DRIVER_PRODUCT_ID "/" CONFIG_MQTT_DRIVER_DEVICE_NAME
#define ONENET_PROPERTY_POST_TOPIC      ONENET_TOPIC_BASE "/thing/property/post"        /* 属性上报 */
#define ONENET_PROPERTY_POST_REPLY      ONENET_TOPIC_BASE "/thing/property/post_reply"  /* 属性上报回复 */
#define ONENET_PROPERTY_SET_TOPIC       ONENET_TOPIC_BASE "/thing/property/set"       /* 属性设置（命令下发） */
#define ONENET_PROPERTY_SET_REPLY       ONENET_TOPIC_BASE "/thing/property/set_reply"

/** @brief 生成的 token 缓冲区（客户端生命周期内需保持有效） */
static char s_onenet_token[512];

/**
 * @brief 对字符串做 URL 编码（仅编码 / + = 三个字符）
 *
 * @param in 输入字符串
 * @param out 输出缓冲区
 * @param out_len 输出缓冲区长度
 * @return None
 */
static void onenet_urlencode(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (const char *p = in; *p != '\0' && o + 3 < out_len; p++) {
        switch (*p) {
        case '/': out[o++] = '%'; out[o++] = '2'; out[o++] = 'F'; break;
        case '+': out[o++] = '%'; out[o++] = '2'; out[o++] = 'B'; break;
        case '=': out[o++] = '%'; out[o++] = '3'; out[o++] = 'D'; break;
        default:  out[o++] = *p; break;
        }
    }
    out[o] = '\0';
}

/**
 * @brief 生成 OneNET 访问 token
 *
 * 算法（version=2018-10-31）：
 *   res  = products/{产品ID}/devices/{设备名称}
 *   et   = 当前时间 + 有效期
 *   org  = "{et}\nsha1\n{res}\n2018-10-31"
 *   sign = base64(hmac_sha1(base64decode(设备密钥), org))，然后 URL 编码
 *   token= version=2018-10-31&res={res}&et={et}&method=sha1&sign={sign}
 *
 * @param token_buf 输出的 token 缓冲区
 * @param buf_len 缓冲区长度
 * @return None
 */
static void onenet_generate_token(char *token_buf, size_t buf_len)
{
    char res[128];
    char res_url[160];
    char org[320];
    char key_bin[64];
    unsigned char sign_raw[32];
    char sign_b64[96];
    char sign_url[128];

    /* 资源字符串：products/{产品ID}/devices/{设备名称}，并做 URL 编码 */
    snprintf(res, sizeof(res), "products/%s/devices/%s",
             CONFIG_MQTT_DRIVER_PRODUCT_ID, CONFIG_MQTT_DRIVER_DEVICE_NAME);
    onenet_urlencode(res, res_url, sizeof(res_url));

    /* 过期时间：当前时间 + 有效期（需先通过 SNTP 同步系统时间） */
    time_t et = time(NULL) + CONFIG_MQTT_DRIVER_TOKEN_VALID_SECONDS;

    /* 待签名串："{et}\nsha1\n{res}\n2018-10-31" */
    snprintf(org, sizeof(org), "%lld\n%s\n%s\n%s",
             (long long)et, ONENET_TOKEN_METHOD, res, ONENET_TOKEN_VERSION);

    /* 设备密钥 base64 解码（HMAC 的密钥为解码后的原始字节） */
    size_t key_len = 0;
    if (mbedtls_base64_decode((unsigned char *)key_bin, sizeof(key_bin), &key_len,
                              (const unsigned char *)CONFIG_MQTT_DRIVER_DEVICE_KEY,
                              strlen(CONFIG_MQTT_DRIVER_DEVICE_KEY)) != 0) {
        ESP_LOGW(TAG, "OneNET device key base64 decode failed");
        key_len = 0;
    }

    /* HMAC-SHA1 签名 */
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1),
                        (const unsigned char *)key_bin, key_len,
                        (const unsigned char *)org, strlen(org), sign_raw) != 0) {
        ESP_LOGW(TAG, "OneNET token HMAC-SHA1 failed");
    }

    /* sign = base64(签名)，再 URL 编码 */
    size_t b64_len = 0;
    mbedtls_base64_encode((unsigned char *)sign_b64, sizeof(sign_b64), &b64_len, sign_raw, 20);
    sign_b64[b64_len] = '\0';
    onenet_urlencode(sign_b64, sign_url, sizeof(sign_url));

    /* 拼接完整 token */
    snprintf(token_buf, buf_len, "version=%s&res=%s&et=%lld&method=%s&sign=%s",
             ONENET_TOKEN_VERSION, res_url, (long long)et, ONENET_TOKEN_METHOD, sign_url);
    ESP_LOGI(TAG, "OneNET token generated (et=%lld)", (long long)et);
}
#endif /* CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH */

/**
 * @brief MQTT 事件处理回调（驱动内部）
 *
 * 由 MQTT 客户端事件循环调用，处理连接、订阅、数据等事件：
 * - 连接成功后订阅主题（OneNET：数据上报响应 + 命令下发）
 * - 订阅确认后发布消息（OneNET：上报 JSON 数据点）
 * - 收到数据时打印，并处理 OneNET 命令下发/回复
 *
 * @param handler_args 注册事件时传入的用户数据（本驱动中不使用，为 NULL）
 * @param base 事件基类型（始终为 MQTT 事件基）
 * @param event_id 收到的事件 ID
 * @param event_data 事件数据，类型为 esp_mqtt_event_handle_t
 * @return None
 */
static void mqtt_evt_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected, subscribing topics");
#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
        /* OneNET Studio：订阅属性上报回复 + 属性设置（命令下发） */
        mqtt_driver_subscribe(ONENET_PROPERTY_POST_REPLY, 0);
        mqtt_driver_subscribe(ONENET_PROPERTY_SET_TOPIC, 0);
#else
        mqtt_driver_subscribe("topic/qos0", 0);
        mqtt_driver_subscribe("topic/qos1", 1);
#endif
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
        /* OneNET Studio：上报物模型属性（OneJSON 格式）
         * 属性标识取自 CONFIG_MQTT_DRIVER_PROPERTY_ID，需与产品物模型一致 */
        {
            char payload[160];
            snprintf(payload, sizeof(payload),
                     "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"%s\":{\"value\":35}}}",
                     CONFIG_MQTT_DRIVER_PROPERTY_ID);
            mqtt_driver_publish(ONENET_PROPERTY_POST_TOPIC, payload, -1, 0, 0);
        }
#else
        mqtt_driver_publish("topic/qos0", "data", 4, 0, 0);
#endif
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data: topic=%.*s data=%.*s",
                 event->topic_len, event->topic, event->data_len, event->data);
#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
        /* OneNET Studio 属性设置（命令下发）：回复到 set_reply 主题
         * 回复需要回传平台下发报文中的 id */
        {
            char topic_buf[128];
            int tlen = event->topic_len < (int)sizeof(topic_buf) - 1 ? event->topic_len : (int)sizeof(topic_buf) - 1;
            memcpy(topic_buf, event->topic, (size_t)tlen);
            topic_buf[tlen] = '\0';
            if (strcmp(topic_buf, ONENET_PROPERTY_SET_TOPIC) == 0) {
                /* 从 JSON 载荷中提取 "id":"xxx" */
                char id_buf[32] = "0";
                const char *id_pos = strstr(event->data, "\"id\"");
                if (id_pos != NULL) {
                    const char *colon = strchr(id_pos, ':');
                    const char *q1 = colon != NULL ? strchr(colon, '"') : NULL;
                    const char *q2 = q1 != NULL ? strchr(q1 + 1, '"') : NULL;
                    if (q1 != NULL && q2 != NULL && (q2 - q1 - 1) < (int)sizeof(id_buf)) {
                        memcpy(id_buf, q1 + 1, (size_t)(q2 - q1 - 1));
                        id_buf[q2 - q1 - 1] = '\0';
                    }
                }
                char resp[96];
                snprintf(resp, sizeof(resp), "{\"id\":\"%s\",\"code\":200}", id_buf);
                mqtt_driver_publish(ONENET_PROPERTY_SET_REPLY, resp, -1, 0, 0);
                ESP_LOGI(TAG, "Property set received, replied on set_reply");
            }
        }
#endif
        break;
    default:
        break;
    }
}

/**
 * @brief 启动 MQTT 驱动
 *
 * 根据 menuconfig 中的配置决定是否启动 MQTT：
 * - 启用 CONFIG_MQTT_DRIVER_ENABLED 时：初始化 MQTT 客户端，
 *   注册内部事件处理回调并启动连接；启用 OneNET 认证时自动生成 token。
 * - 未启用时：打印警告日志并直接返回。
 *
 * @param None
 * @return None
 */
void mqtt_driver_start(void)
{
#if CONFIG_MQTT_DRIVER_ENABLED
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = CONFIG_MQTT_DRIVER_BROKER_URI,
        },
    };

#if CONFIG_MQTT_DRIVER_BROKER_CERTIFICATE_OVERRIDDEN
    mqtt_cfg.broker.verification.certificate = cert_override_pem;
#elif CONFIG_MQTT_DRIVER_CERT_VALIDATE_MOSQUITTO_CA
    mqtt_cfg.broker.verification.certificate = (const char *)mosquitto_org_crt_start;
#else
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach; /* 使用内置证书包 */
#endif

#if CONFIG_MQTT_DRIVER_ENABLE_ONENET_AUTH
    /* OneNET 认证：username=产品ID，clientId=设备名称，password=token */
    onenet_generate_token(s_onenet_token, sizeof(s_onenet_token));
    mqtt_cfg.credentials.username = CONFIG_MQTT_DRIVER_PRODUCT_ID;
    mqtt_cfg.credentials.client_id = CONFIG_MQTT_DRIVER_DEVICE_NAME;
    mqtt_cfg.credentials.authentication.password = s_onenet_token;
#endif

    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    /* 注册驱动内部事件处理回调 */
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_evt_handler, NULL);
    esp_mqtt_client_start(s_client);
#else
    ESP_LOGW(TAG, "MQTT disabled: CONFIG_MQTT_DRIVER_ENABLED is not set");
#endif
}

/**
 * @brief 发布消息到指定主题
 *
 * @param topic 目标主题（C 字符串）
 * @param data 消息数据（C 字符串，也可为任意字节流）
 * @param len 数据长度（字节）
 * @param qos QoS 等级（0/1/2）
 * @param retain 是否保留消息
 * @return 消息 ID；驱动未启动或发送失败时返回 -1
 */
int mqtt_driver_publish(const char *topic, const char *data, int len, int qos, int retain)
{
    if (s_client == NULL) {
        return -1;
    }
    return esp_mqtt_client_publish(s_client, topic, data, len, qos, retain);
}

/**
 * @brief 订阅主题
 *
 * @param topic 要订阅的主题（C 字符串）
 * @param qos QoS 等级（0/1/2）
 * @return 消息 ID；驱动未启动或订阅失败时返回 -1
 */
int mqtt_driver_subscribe(const char *topic, int qos)
{
    if (s_client == NULL) {
        return -1;
    }
    return esp_mqtt_client_subscribe(s_client, topic, qos);
}

/**
 * @brief 取消订阅主题
 *
 * @param topic 要取消订阅的主题（C 字符串）
 * @return 消息 ID；驱动未启动或取消订阅失败时返回 -1
 */
int mqtt_driver_unsubscribe(const char *topic)
{
    if (s_client == NULL) {
        return -1;
    }
    return esp_mqtt_client_unsubscribe(s_client, topic);
}
