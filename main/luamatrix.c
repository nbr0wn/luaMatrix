#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_wifi.h"
//#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "http_server.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "captive_portal.h"
#include "display.h"
#include "local_lua.h"
#include "luamatrix_mqtt.h"

#define WIFI_SCAN_LIST_SIZE 10

static const char* TAG = "main";

// Initialize and mount the filesystem
void init_filesystem()
{
    ESP_LOGI(TAG, "Initializing File System");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = LUA_FILE_PATH,
        .partition_label = "assets",
        .partition=NULL,
        .format_if_mount_failed = false,
        .read_only=false,
        .dont_mount = false,
        .grow_on_mount=false
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else {
        ESP_LOGI(TAG, "Filesystem mounted at %s", LUA_FILE_PATH);
    }
}

bool ether_connected = false;

void wifi_event_cb(wifi_config_event_t event)
{
    if (event == WIFI_CONFIG_EVENT_CONNECTED) {
        ether_connected = true;
        printf("WiFi connected!\n");
        // Start MQTT client when WiFi connects
        mqtt_client_start();
    } else if (event == WIFI_CONFIG_EVENT_DISCONNECTED) {
        printf("WiFi disconnected!\n");
        ether_connected = false;
        // Stop MQTT client when WiFi disconnects
        mqtt_client_stop();
    }
}

void app_main(void)
{
    // Init the LED matrix panel
    display_init();

    // Initialize and mount the filesystem
    init_filesystem();

    // Initialize MQTT (loads config from NVS)
    mqtt_client_init();

    //esp_task_wdt_delete(NULL);
    wifi_config_init("LuaMatrix", NULL, wifi_event_cb);

    // Wait for connection
    while (1) {
        if (!ether_connected) {
            ESP_LOGI(TAG, "NOT CONNECTED");

            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            // Wait for a couple seconds after connection - we get
            // a connect hit before password validation has happened
            vTaskDelay(pdMS_TO_TICKS(2000));
            if(ether_connected) {
                ESP_LOGI(TAG, "CONNECTED!");
                break;
            }
        }
    }

    // Start the management webserver
    mgmt_http_server_start();

    // Prevent the task from ending
    while (1) {
        run_lua_file("display.lua");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}