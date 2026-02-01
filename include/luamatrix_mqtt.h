#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MQTT_MAX_TOPIC_LEN 128

// Configuration management
void mqtt_config_load(void);
void mqtt_config_save(void);
void mqtt_config_set_broker(const char *url, uint16_t port);
void mqtt_config_set_auth(const char *username, const char *password);
void mqtt_config_set_topics(const char *data_topic, const char *program_topic);
void mqtt_config_set_enabled(bool enabled);

// Getters for HTTP handlers
void mqtt_config_get_broker(char *url, size_t len, uint16_t *port);
void mqtt_config_get_auth(char *user, size_t ulen, char *pass, size_t plen);
void mqtt_config_get_topics(char *data_topic, size_t dlen, char *program_topic, size_t plen);
bool mqtt_config_get_enabled(void);

// Client lifecycle
esp_err_t mqtt_client_init(void);
esp_err_t mqtt_client_start(void);
esp_err_t mqtt_client_stop(void);
bool mqtt_client_is_connected(void);

// Publishing
esp_err_t mqtt_publish(const char *topic, const char *data, int qos, int retain);

// For Lua - get pending messages from queue
bool mqtt_get_pending_message(char *topic, size_t tlen, char *data, size_t dlen);

// Blocking wait for message (returns false if timeout or not connected)
bool mqtt_wait_for_message(char *topic, size_t tlen, char *data, size_t dlen, uint32_t timeout_ms);
