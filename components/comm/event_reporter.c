/*
 * event_reporter.c — UART JSON 事件上报
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "pin_defs.h"
#include "sys_config.h"

static const char *TAG = "COMM";

static bool s_initialized = false;

/* ================================================================ */

esp_err_t event_reporter_init(void)
{
    uart_config_t uart_config = {
        .baud_rate  = COMM_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(UART_PORT, 256, 256, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_PORT);
        return ret;
    }

    ret = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_PORT);
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "UART initialized: TX=%d RX=%d @ %d baud",
             UART_TX_PIN, UART_RX_PIN, COMM_UART_BAUDRATE);
    return ESP_OK;
}

/* ================================================================ */

static esp_err_t wait_for_ok(void)
{
    uint8_t buf[32];
    int64_t deadline = esp_timer_get_time() + COMM_RESPONSE_TIMEOUT_MS * 1000;

    while (esp_timer_get_time() < deadline) {
        int len = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1,
                                  pdMS_TO_TICKS(100));
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGD(TAG, "UART RX: \"%s\"", buf);

            if (strstr((const char *)buf, "OK") != NULL) {
                return ESP_OK;
            }
        }
    }

    ESP_LOGW(TAG, "UART response timeout (%dms)", COMM_RESPONSE_TIMEOUT_MS);
    return ESP_ERR_TIMEOUT;
}

/* ================================================================ */

esp_err_t event_reporter_send_wake(float confidence, float similarity)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "UART not initialized — skipping report");
        return ESP_ERR_INVALID_STATE;
    }

    /* 构建 JSON */
    int64_t now_sec = esp_timer_get_time() / 1000000;
    char json[256];
    int len = snprintf(json, sizeof(json),
        "{\"dev\":\"esp32c3_kws_01\","
        "\"evt\":\"wake\","
        "\"conf\":%.4f,"
        "\"sim\":%.4f,"
        "\"ts\":%lld}\r\n",
        confidence, similarity, now_sec);

    if (len < 0 || len >= (int)sizeof(json)) {
        ESP_LOGE(TAG, "JSON too long (%d bytes)", len);
        return ESP_ERR_NO_MEM;
    }

    /* 发送（最多重试 COMM_MAX_RETRIES 次） */
    for (int attempt = 0; attempt <= COMM_MAX_RETRIES; attempt++) {
        ESP_LOGI(TAG, "Sending wake event (attempt %d): %s", attempt + 1, json);

        int sent = uart_write_bytes(UART_PORT, json, len);
        if (sent != len) {
            ESP_LOGE(TAG, "UART write failed: %d/%d", sent, len);
            continue;
        }

        if (COMM_MAX_RETRIES == 0) {
            /* 不等待回显 */
            return ESP_OK;
        }

        esp_err_t ret = wait_for_ok();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Wake event ACKed by module");
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "Wake event not ACKed after %d retries", COMM_MAX_RETRIES);
    return ESP_ERR_TIMEOUT;
}
