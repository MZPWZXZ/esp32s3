/**
 * @file mqtt_driver.c
 * @brief MQTT 客户端驱动实现
 *
 * 本文件仅实现 MQTT 驱动能力：客户端初始化、启动、
 * 发布/订阅/取消订阅、事件分发，以及 OneNET 平台的
 * token 生成与 CONNECT 认证（不包含任何业务逻辑）。
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

/** @brief 业务层注册的事件回调及其参数 */
static mqtt_driver_event_cb_t s_evt_cb = NULL;
static void *s_evt_arg = NULL;

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
 * 记录事件日志，并将事件分发到业务层注册的回调 s_evt_cb。
 *
 * @param handler_args 注册事件时传入的用户数据（本驱动中不使用，为 NULL）
 * @param base 事件基类型（始终为 MQTT 事件基）
 * @param event_id 收到的事件 ID
 * @param event_data 事件数据，类型为 esp_mqtt_event_handle_t
 * @return None
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    if (s_evt_cb != NULL) {
        s_evt_cb(event_id, event_data, s_evt_arg);
    }
}

/**
 * @brief 启动 MQTT 驱动
 *
 * 根据 menuconfig 中的配置决定是否启动 MQTT：
 * - 启用 CONFIG_MQTT_DRIVER_ENABLED 时：初始化 MQTT 客户端，
 *   注册事件分发回调并启动连接；启用 OneNET 认证时自动生成 token。
 * - 未启用时：打印警告日志并直接返回。
 *
 * @param evt_cb 事件回调函数指针（可为 NULL，仅记录日志不回调）
 * @param evt_arg 回调的用户参数（可为 NULL）
 * @return None
 */
void mqtt_driver_start(mqtt_driver_event_cb_t evt_cb, void *evt_arg)
{
#if CONFIG_MQTT_DRIVER_ENABLED
    s_evt_cb = evt_cb;
    s_evt_arg = evt_arg;

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
    /* 注册事件分发回调（业务逻辑由 mqtt_driver_event_cb_t 回调处理） */
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
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
