/**
 * MQTT Client implementation for luaMatrix
 */

#include "luamatrix_mqtt.h"
#include "mqtt_client.h"  // ESP-IDF mqtt_client
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// From mgmt_http_server.c - triggers Lua script reload
extern bool force_exit;

#define MQTT_NAMESPACE "mqtt_config"

static const char *TAG = "mqtt";

// MQTT configuration structure
typedef struct {
    char broker_url[256];
    uint16_t port;
    char username[64];
    char password[64];
    char data_topic[MQTT_MAX_TOPIC_LEN];
    char program_topic[MQTT_MAX_TOPIC_LEN];
    bool enabled;
} mqtt_config_t;

// Message structure for queue
typedef struct {
    char topic[MQTT_MAX_TOPIC_LEN];
    char *data;
    int data_len;
} mqtt_msg_t;

// Static state
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static mqtt_config_t s_config = {0};
static SemaphoreHandle_t s_mutex = NULL;
static bool s_connected = false;
static QueueHandle_t s_msg_queue = NULL;

#define MQTT_MSG_QUEUE_SIZE 10

// Forward declarations
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data);
static void mqtt_subscribe_configured_topics(void);

// ============================================================================
// NVS Configuration Storage
// ============================================================================

void mqtt_config_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(MQTT_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        // Set defaults
        s_config.port = 1883;
        s_config.enabled = false;
        s_config.data_topic[0] = '\0';
        s_config.program_topic[0] = '\0';
        ESP_LOGI(TAG, "No saved MQTT config, using defaults");
        return;
    }

    size_t required;

    required = sizeof(s_config.broker_url);
    if (nvs_get_str(nvs, "broker_url", s_config.broker_url, &required) != ESP_OK) {
        s_config.broker_url[0] = '\0';
    }

    if (nvs_get_u16(nvs, "port", &s_config.port) != ESP_OK) {
        s_config.port = 1883;
    }

    required = sizeof(s_config.username);
    if (nvs_get_str(nvs, "username", s_config.username, &required) != ESP_OK) {
        s_config.username[0] = '\0';
    }

    required = sizeof(s_config.password);
    if (nvs_get_str(nvs, "password", s_config.password, &required) != ESP_OK) {
        s_config.password[0] = '\0';
    }

    uint8_t enabled = 0;
    if (nvs_get_u8(nvs, "enabled", &enabled) == ESP_OK) {
        s_config.enabled = (enabled != 0);
    } else {
        s_config.enabled = false;
    }

    required = sizeof(s_config.data_topic);
    if (nvs_get_str(nvs, "data_topic", s_config.data_topic, &required) != ESP_OK) {
        s_config.data_topic[0] = '\0';
    }

    required = sizeof(s_config.program_topic);
    if (nvs_get_str(nvs, "program_topic", s_config.program_topic, &required) != ESP_OK) {
        s_config.program_topic[0] = '\0';
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "MQTT config loaded: broker=%s, port=%d, enabled=%d",
             s_config.broker_url, s_config.port, s_config.enabled);
}

void mqtt_config_save(void)
{
    nvs_handle_t nvs;
    if (nvs_open(MQTT_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return;
    }

    nvs_set_str(nvs, "broker_url", s_config.broker_url);
    nvs_set_u16(nvs, "port", s_config.port);
    nvs_set_str(nvs, "username", s_config.username);
    nvs_set_str(nvs, "password", s_config.password);
    nvs_set_u8(nvs, "enabled", s_config.enabled ? 1 : 0);
    nvs_set_str(nvs, "data_topic", s_config.data_topic);
    nvs_set_str(nvs, "program_topic", s_config.program_topic);

    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "MQTT config saved");
}

void mqtt_config_set_broker(const char *url, uint16_t port)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_config.broker_url, url, sizeof(s_config.broker_url) - 1);
    s_config.broker_url[sizeof(s_config.broker_url) - 1] = '\0';
    s_config.port = port;
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void mqtt_config_set_auth(const char *username, const char *password)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_config.username, username ? username : "", sizeof(s_config.username) - 1);
    s_config.username[sizeof(s_config.username) - 1] = '\0';
    strncpy(s_config.password, password ? password : "", sizeof(s_config.password) - 1);
    s_config.password[sizeof(s_config.password) - 1] = '\0';
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void mqtt_config_set_topics(const char *data_topic, const char *program_topic)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_config.data_topic, data_topic ? data_topic : "", MQTT_MAX_TOPIC_LEN - 1);
    s_config.data_topic[MQTT_MAX_TOPIC_LEN - 1] = '\0';
    strncpy(s_config.program_topic, program_topic ? program_topic : "", MQTT_MAX_TOPIC_LEN - 1);
    s_config.program_topic[MQTT_MAX_TOPIC_LEN - 1] = '\0';
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void mqtt_config_set_enabled(bool enabled)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_config.enabled = enabled;
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void mqtt_config_get_broker(char *url, size_t len, uint16_t *port)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (url && len > 0) {
        strncpy(url, s_config.broker_url, len - 1);
        url[len - 1] = '\0';
    }
    if (port) {
        *port = s_config.port;
    }
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void mqtt_config_get_auth(char *user, size_t ulen, char *pass, size_t plen)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (user && ulen > 0) {
        strncpy(user, s_config.username, ulen - 1);
        user[ulen - 1] = '\0';
    }
    if (pass && plen > 0) {
        strncpy(pass, s_config.password, plen - 1);
        pass[plen - 1] = '\0';
    }
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void mqtt_config_get_topics(char *data_topic, size_t dlen, char *program_topic, size_t plen)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (data_topic && dlen > 0) {
        strncpy(data_topic, s_config.data_topic, dlen - 1);
        data_topic[dlen - 1] = '\0';
    }
    if (program_topic && plen > 0) {
        strncpy(program_topic, s_config.program_topic, plen - 1);
        program_topic[plen - 1] = '\0';
    }
    if (s_mutex) xSemaphoreGive(s_mutex);
}

bool mqtt_config_get_enabled(void)
{
    bool enabled;
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    enabled = s_config.enabled;
    if (s_mutex) xSemaphoreGive(s_mutex);
    return enabled;
}

// ============================================================================
// MQTT Client Lifecycle
// ============================================================================

esp_err_t mqtt_client_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_FAIL;
        }
    }

    if (s_msg_queue == NULL) {
        s_msg_queue = xQueueCreate(MQTT_MSG_QUEUE_SIZE, sizeof(mqtt_msg_t));
        if (s_msg_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create message queue");
            return ESP_FAIL;
        }
    }

    mqtt_config_load();
    ESP_LOGI(TAG, "MQTT client initialized");
    return ESP_OK;
}

esp_err_t mqtt_client_start(void)
{
    if (!s_config.enabled) {
        ESP_LOGI(TAG, "MQTT not enabled");
        return ESP_OK;
    }

    if (strlen(s_config.broker_url) == 0) {
        ESP_LOGI(TAG, "No MQTT broker configured");
        return ESP_OK;
    }

    if (s_mqtt_client != NULL) {
        mqtt_client_stop();
    }

    char uri[300];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", s_config.broker_url, s_config.port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
    };

    if (strlen(s_config.username) > 0) {
        mqtt_cfg.credentials.username = s_config.username;
        mqtt_cfg.credentials.authentication.password = s_config.password;
    }

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started, connecting to %s", uri);
    return ESP_OK;
}

esp_err_t mqtt_client_stop(void)
{
    if (s_mqtt_client == NULL) {
        return ESP_OK;
    }

    esp_mqtt_client_stop(s_mqtt_client);
    esp_mqtt_client_destroy(s_mqtt_client);
    s_mqtt_client = NULL;
    s_connected = false;

    // Clear any pending messages
    mqtt_msg_t msg;
    while (xQueueReceive(s_msg_queue, &msg, 0) == pdTRUE) {
        if (msg.data) {
            free(msg.data);
        }
    }

    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

bool mqtt_client_is_connected(void)
{
    return s_connected;
}

// ============================================================================
// MQTT Event Handler
// ============================================================================

static void mqtt_subscribe_configured_topics(void)
{
    if (s_mqtt_client == NULL || !s_connected) {
        return;
    }

    if (strlen(s_config.data_topic) > 0) {
        int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_config.data_topic, 0);
        ESP_LOGI(TAG, "Subscribed to data topic: %s, msg_id=%d", s_config.data_topic, msg_id);
    }

    if (strlen(s_config.program_topic) > 0) {
        int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_config.program_topic, 0);
        ESP_LOGI(TAG, "Subscribed to program topic: %s, msg_id=%d", s_config.program_topic, msg_id);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        s_connected = true;
        mqtt_subscribe_configured_topics();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        s_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT Subscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT Unsubscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT Published, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT Data received on topic: %.*s",
                 event->topic_len, event->topic);

        // Check if this is the program topic - save to display.lua and reload
        if (strlen(s_config.program_topic) > 0 &&
            event->topic_len == (int)strlen(s_config.program_topic) &&
            strncmp(event->topic, s_config.program_topic, event->topic_len) == 0) {

            ESP_LOGI(TAG, "Program update received, saving to display.lua");
            FILE *fp = fopen("/assets/display.lua", "w");
            if (fp) {
                fwrite(event->data, 1, event->data_len, fp);
                fclose(fp);
                ESP_LOGI(TAG, "Saved %d bytes to display.lua", event->data_len);
                force_exit = true;
            } else {
                ESP_LOGE(TAG, "Failed to open display.lua for writing");
            }
        }

        // Queue message for Lua (data topic messages)
        if (s_msg_queue && event->topic_len > 0) {
            mqtt_msg_t msg = {0};

            int topic_len = (event->topic_len < MQTT_MAX_TOPIC_LEN - 1) ?
                            event->topic_len : MQTT_MAX_TOPIC_LEN - 1;
            memcpy(msg.topic, event->topic, topic_len);
            msg.topic[topic_len] = '\0';

            msg.data = malloc(event->data_len + 1);
            if (msg.data) {
                memcpy(msg.data, event->data, event->data_len);
                msg.data[event->data_len] = '\0';
                msg.data_len = event->data_len;

                if (xQueueSend(s_msg_queue, &msg, 0) != pdTRUE) {
                    free(msg.data);
                    ESP_LOGW(TAG, "MQTT message queue full, dropping message");
                }
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Error");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "Transport error: %s", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGD(TAG, "MQTT event: %ld", event_id);
        break;
    }
}

// ============================================================================
// Publishing and Message Retrieval
// ============================================================================

esp_err_t mqtt_publish(const char *topic, const char *data, int qos, int retain)
{
    if (s_mqtt_client == NULL || !s_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish");
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data,
                                         data ? strlen(data) : 0, qos, retain);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Published to %s, msg_id=%d", topic, msg_id);
    return ESP_OK;
}

bool mqtt_get_pending_message(char *topic, size_t tlen, char *data, size_t dlen)
{
    if (s_msg_queue == NULL) {
        return false;
    }

    mqtt_msg_t msg;
    if (xQueueReceive(s_msg_queue, &msg, 0) != pdTRUE) {
        return false;
    }

    if (topic && tlen > 0) {
        strncpy(topic, msg.topic, tlen - 1);
        topic[tlen - 1] = '\0';
    }

    if (data && dlen > 0 && msg.data) {
        strncpy(data, msg.data, dlen - 1);
        data[dlen - 1] = '\0';
    }

    if (msg.data) {
        free(msg.data);
    }

    return true;
}

bool mqtt_wait_for_message(char *topic, size_t tlen, char *data, size_t dlen, uint32_t timeout_ms)
{
    if (s_msg_queue == NULL) {
        return false;
    }

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    mqtt_msg_t msg;
    if (xQueueReceive(s_msg_queue, &msg, ticks) != pdTRUE) {
        return false;
    }

    if (topic && tlen > 0) {
        strncpy(topic, msg.topic, tlen - 1);
        topic[tlen - 1] = '\0';
    }

    if (data && dlen > 0 && msg.data) {
        strncpy(data, msg.data, dlen - 1);
        data[dlen - 1] = '\0';
    }

    if (msg.data) {
        free(msg.data);
    }

    return true;
}
