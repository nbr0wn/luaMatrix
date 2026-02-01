#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_system.h"
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
#include "luafuncs.h"
#include "luamatrix_mqtt.h"

#define WIFI_SCAN_LIST_SIZE 10

static const char* TAG = "main";

// Boot screen colors
#define BOOT_TITLE_R 0
#define BOOT_TITLE_G 128
#define BOOT_TITLE_B 255

#define BOOT_STATUS_R 200
#define BOOT_STATUS_G 200
#define BOOT_STATUS_B 200

#define BOOT_PORTAL_R 255
#define BOOT_PORTAL_G 180
#define BOOT_PORTAL_B 0

// Show boot status on the LED matrix
static void boot_show_status(const char *status) {
    clear_display();
    draw_text("LuaMatrix", 2, 2, BOOT_TITLE_R, BOOT_TITLE_G, BOOT_TITLE_B, 8);
    draw_text(status, 2, 14, BOOT_STATUS_R, BOOT_STATUS_G, BOOT_STATUS_B, 5);
}


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

    // Show boot screen
    boot_show_status("Booting...");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize and mount the filesystem
    boot_show_status("Init FS...");
    init_filesystem();

    // Initialize MQTT (loads config from NVS)
    boot_show_status("Init MQTT...");
    mqtt_client_init();

    // Initialize WiFi
    boot_show_status("Init WiFi...");
    wifi_config_init("LuaMatrix", NULL, wifi_event_cb);

    // Get configured SSID from ESP-IDF WiFi config
    wifi_config_t wifi_cfg;
    char ssid_str[33] = "";
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK) {
        strncpy(ssid_str, (char *)wifi_cfg.sta.ssid, sizeof(ssid_str) - 1);
        ssid_str[sizeof(ssid_str) - 1] = '\0';
    }

    // Wait for connection (with timeout to handle stuck WiFi)
    int wait_count = 0;
    const int wifi_timeout_secs = 20;
    while (1) {
        if (!ether_connected) {
            int remaining = wifi_timeout_secs - wait_count;
            ESP_LOGI(TAG, "NOT CONNECTED (%d)", remaining);

            // Reboot if we've been waiting too long
            if (remaining <= 0) {
                ESP_LOGW(TAG, "WiFi connection timeout, rebooting...");
                boot_show_status("WiFi timeout");
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            }

            // Show SSID with countdown
            char status[64];
            if (ssid_str[0] != '\0') {
                snprintf(status, sizeof(status), "SSID:%s (%d)", ssid_str, remaining);
            } else {
                snprintf(status, sizeof(status), "Connecting (%d)", remaining);
            }
            boot_show_status(status);

            wait_count++;
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            if(ether_connected) {
                ESP_LOGI(TAG, "CONNECTED!");
                boot_show_status("Connected!");
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
            }
        }
    }

    // Start the management webserver
    boot_show_status("Starting...");
    mgmt_http_server_start();

    boot_show_status("Ready!");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Prevent the task from ending
    while (1) {
        run_lua_file("display.lua");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}