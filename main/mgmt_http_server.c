/**
   Copyright 2025 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NON INFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   for more information visit https://www.studiopieters.nl
 **/

#include "esp_http_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

#include "esp_littlefs.h"
#include "luamatrix_mqtt.h"

// Binary-embedded templates
#define DECLARE_TEMPLATE(name) \
        extern const char _binary_##name##_start[] asm("_binary_" #name "_start"); \
        extern const char _binary_##name##_end[] asm("_binary_" #name "_end")

#define TEMPLATE(name)     _binary_##name##_start
#define TEMPLATE_LEN(name) (_binary_##name##_end - _binary_##name##_start)

DECLARE_TEMPLATE(favicon_svg);
DECLARE_TEMPLATE(mgmt_index_html);
DECLARE_TEMPLATE(logout_html);
DECLARE_TEMPLATE(edit_html);
DECLARE_TEMPLATE(firmware_html);
DECLARE_TEMPLATE(reboot_html);
#include "esp_vfs.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

#define UPLOAD_BUF_SIZE 1024
//#include "sdkconfig.h"
//#include "errno.h"
//#include "esp_err.h"
//#include "esp_heap_caps.h"
//#include "esp_idf_version.h"
//#include "esp_partition.h"
//#include "esp_rom_sys.h"
//#include "esp_spiffs.h"
//#include "esp_system.h"
//#include "esp_timer.h"
//#include "freertos/FreeRTOS.h"
//#include "freertos/queue.h"
//#include "freertos/semphr.h"
//#include "freertos/task.h"
//#include <fcntl.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <sys/param.h>
//#include <sys/time.h>
//#include <sys/unistd.h>
//#include <time.h>

static const char* TAG = "http";

static int hex_to_int(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
}

static void url_decode(char *str) {
        char *src = str, *dst = str;
        while (*src) {
                if (*src == '%' && src[1] && src[2]) {
                        int hi = hex_to_int(src[1]);
                        int lo = hex_to_int(src[2]);
                        if (hi >= 0 && lo >= 0) {
                                *dst++ = (char)((hi << 4) | lo);
                                src += 3;
                                continue;
                        }
                } else if (*src == '+') {
                        *dst++ = ' ';
                        src++;
                        continue;
                }
                *dst++ = *src++;
        }
        *dst = '\0';
}

static httpd_handle_t server = NULL;

static esp_err_t index_handler(httpd_req_t *req) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, TEMPLATE(mgmt_index_html), TEMPLATE_LEN(mgmt_index_html));
        return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req) {
        httpd_resp_set_type(req, "image/svg+xml");
        httpd_resp_send(req, TEMPLATE(favicon_svg), TEMPLATE_LEN(favicon_svg));
        return ESP_OK;
}

#define MAX_FILENAME_LEN 64

// Returns JSON array of files: [{"name":"foo.lua","size":1234}, ...]
static esp_err_t files_handler(httpd_req_t *req) {
        httpd_resp_set_type(req, "application/json");

        DIR *dir = opendir("/assets");
        if (!dir) {
                httpd_resp_sendstr(req, "[]");
                return ESP_OK;
        }

        httpd_resp_sendstr_chunk(req, "[");

        // Buffer sizes: entry needs ~105 bytes max, filepath needs ~73 bytes max
        // Using larger buffers for safety margin
        char entry[128];
        char filepath[80];
        char filename[MAX_FILENAME_LEN + 1];
        struct stat st;
        int first = 1;

        while (1) {
                struct dirent *de = readdir(dir);
                if (!de) break;
                if (de->d_name[0] == '.') continue;

                // Truncate filename to MAX_FILENAME_LEN
                strncpy(filename, de->d_name, MAX_FILENAME_LEN);
                filename[MAX_FILENAME_LEN] = '\0';

                snprintf(filepath, sizeof(filepath), "/assets/%s", filename);
                long filesize = 0;
                if (stat(filepath, &st) == 0) {
                        filesize = st.st_size;
                }

                snprintf(entry, sizeof(entry), "%s{\"name\":\"%s\",\"size\":%ld}",
                        first ? "" : ",", filename, filesize);
                httpd_resp_sendstr_chunk(req, entry);
                first = 0;
        }

        closedir(dir);
        httpd_resp_sendstr_chunk(req, "]");
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
}

// Returns free space in bytes as JSON: {"free":12345}
static esp_err_t freespace_handler(httpd_req_t *req) {
        httpd_resp_set_type(req, "application/json");

        size_t total = 0, used = 0;
        esp_littlefs_info("assets", &total, &used);
        size_t free_bytes = (total > used) ? (total - used) : 0;

        char response[64];
        snprintf(response, sizeof(response), "{\"free\":%zu,\"total\":%zu,\"used\":%zu}", free_bytes, total, used);
        httpd_resp_sendstr(req, response);
        return ESP_OK;
}

// Extract filename from Content-Disposition header
// Looks for: filename="something.lua"
static int extract_filename(const char *buf, int len, char *filename, int max_len) {
        const char *needle = "filename=\"";
        char *start = memmem(buf, len, needle, strlen(needle));
        if (!start) return -1;

        start += strlen(needle);
        char *end = memchr(start, '"', len - (start - buf));
        if (!end) return -1;

        int name_len = end - start;
        if (name_len <= 0 || name_len >= max_len) return -1;

        // Copy and truncate to MAX_FILENAME_LEN
        if (name_len > MAX_FILENAME_LEN) name_len = MAX_FILENAME_LEN;
        memcpy(filename, start, name_len);
        filename[name_len] = '\0';
        return 0;
}

// Find end of multipart headers (double CRLF)
static char *find_content_start(char *buf, int len) {
        char *pos = memmem(buf, len, "\r\n\r\n", 4);
        if (pos) return pos + 4;
        return NULL;
}

// File delete handler - deletes file from /assets
static esp_err_t delete_handler(httpd_req_t *req) {
        char query[128];
        char filename[MAX_FILENAME_LEN + 1] = {0};
        char filepath[80];

        // Get query string
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
                return ESP_FAIL;
        }

        // Extract filename parameter
        if (httpd_query_key_value(query, "filename", filename, sizeof(filename)) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename");
                return ESP_FAIL;
        }

        // URL decode the filename
        url_decode(filename);

        // Construct full path
        snprintf(filepath, sizeof(filepath), "/assets/%s", filename);

        // Delete the file
        if (unlink(filepath) != 0) {
                ESP_LOGE(TAG, "Failed to delete file: %s", filepath);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
                return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Deleted file: %s", filepath);

        // Redirect back to main page
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
}

// Edit page handler - serves the ACE editor with filename substituted
static esp_err_t edit_handler(httpd_req_t *req) {
        char query[128];
        char filename[MAX_FILENAME_LEN + 1] = {0};

        // Get query string
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
                return ESP_FAIL;
        }

        // Extract filename parameter
        if (httpd_query_key_value(query, "filename", filename, sizeof(filename)) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename");
                return ESP_FAIL;
        }

        url_decode(filename);

        httpd_resp_set_type(req, "text/html");

        // Send the edit_html template with %FILENAME% replaced
        const char *src = TEMPLATE(edit_html);
        const char *end = src + TEMPLATE_LEN(edit_html);
        const char *placeholder = "%FILENAME%";
        const char *errortext = "%ERRORTEXT%";
        size_t placeholder_len = strlen(placeholder);
        size_t errortext_len = strlen(errortext);

        while (src < end) {
                // Search within remaining bounds
                const char *pos = NULL;
                const char *errpos = NULL;
                size_t remaining = end - src;

                // Find %FILENAME%
                for (const char *p = src; p + placeholder_len <= end; p++) {
                        if (memcmp(p, placeholder, placeholder_len) == 0) {
                                pos = p;
                                break;
                        }
                }

                // Find %ERRORTEXT%
                for (const char *p = src; p + errortext_len <= end; p++) {
                        if (memcmp(p, errortext, errortext_len) == 0) {
                                errpos = p;
                                break;
                        }
                }

                // Find which placeholder comes first
                const char *first = NULL;
                int is_filename = 0;
                if (pos && (!errpos || pos < errpos)) {
                        first = pos;
                        is_filename = 1;
                } else if (errpos) {
                        first = errpos;
                        is_filename = 0;
                }

                if (first) {
                        // Send text before placeholder
                        if (first > src) {
                                httpd_resp_send_chunk(req, src, first - src);
                        }
                        // Send replacement
                        if (is_filename) {
                                httpd_resp_sendstr_chunk(req, filename);
                                src = first + placeholder_len;
                        } else {
                                // Empty error text
                                src = first + errortext_len;
                        }
                } else {
                        // No more placeholders, send rest
                        httpd_resp_send_chunk(req, src, end - src);
                        break;
                }
        }

        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
}

// Read file contents handler - returns raw file content
static esp_err_t readfile_handler(httpd_req_t *req) {
        char query[128];
        char filename[MAX_FILENAME_LEN + 1] = {0};
        char filepath[80];

        // Get query string
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
                return ESP_FAIL;
        }

        // Extract filename parameter
        if (httpd_query_key_value(query, "filename", filename, sizeof(filename)) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename");
                return ESP_FAIL;
        }

        url_decode(filename);
        snprintf(filepath, sizeof(filepath), "/assets/%s", filename);

        FILE *fp = fopen(filepath, "r");
        if (!fp) {
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
                return ESP_FAIL;
        }

        httpd_resp_set_type(req, "text/plain");

        char buf[512];
        size_t read_len;
        while ((read_len = fread(buf, 1, sizeof(buf), fp)) > 0) {
                httpd_resp_send_chunk(req, buf, read_len);
        }

        fclose(fp);
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
}

bool force_exit = false;

// Save file handler - receives file content in POST body
static esp_err_t save_handler(httpd_req_t *req) {
        char filename[MAX_FILENAME_LEN + 1] = {0};
        char filepath[80];

        // Get filename from header
        if (httpd_req_get_hdr_value_str(req, "filename", filename, sizeof(filename)) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename header");
                return ESP_FAIL;
        }

        url_decode(filename);
        snprintf(filepath, sizeof(filepath), "/assets/%s", filename);

        FILE *fp = fopen(filepath, "w");
        if (!fp) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
                return ESP_FAIL;
        }

        char buf[512];
        int remaining = req->content_len;

        while (remaining > 0) {
                int to_read = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
                int ret = httpd_req_recv(req, buf, to_read);
                if (ret <= 0) {
                        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
                        fclose(fp);
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
                        return ESP_FAIL;
                }
                fwrite(buf, 1, ret, fp);
                remaining -= ret;
        }

        fclose(fp);
        ESP_LOGI(TAG, "Saved file: %s", filepath);
        httpd_resp_sendstr(req, "OK");
        force_exit = true;
        return ESP_OK;
}

// File upload handler - receives multipart/form-data
static esp_err_t upload_handler(httpd_req_t *req) {
        char *buf = malloc(UPLOAD_BUF_SIZE);
        if (!buf) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
                return ESP_FAIL;
        }

        char filename[MAX_FILENAME_LEN + 1] = {0};
        char filepath[80];
        FILE *fp = NULL;
        int remaining = req->content_len;
        int received = 0;
        int header_parsed = 0;
        char boundary[128] = {0};
        int boundary_len = 0;

        // Get boundary from Content-Type header
        char content_type[256];
        if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK) {
                char *b = strstr(content_type, "boundary=");
                if (b) {
                        b += 9;
                        snprintf(boundary, sizeof(boundary), "--%s", b);
                        boundary_len = strlen(boundary);
                }
        }

        if (boundary_len == 0) {
                free(buf);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing boundary");
                return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Upload starting, content_len=%d, boundary=%s", req->content_len, boundary);

        while (remaining > 0) {
                int to_read = (remaining < UPLOAD_BUF_SIZE) ? remaining : UPLOAD_BUF_SIZE;
                int ret = httpd_req_recv(req, buf, to_read);
                if (ret <= 0) {
                        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
                        ESP_LOGE(TAG, "Upload receive error");
                        break;
                }
                remaining -= ret;
                received += ret;

                if (!header_parsed) {
                        // Parse multipart headers to get filename
                        if (extract_filename(buf, ret, filename, sizeof(filename)) == 0) {
                                snprintf(filepath, sizeof(filepath), "/assets/%s", filename);
                                ESP_LOGI(TAG, "Uploading file: %s", filepath);

                                fp = fopen(filepath, "w");
                                if (!fp) {
                                        ESP_LOGE(TAG, "Failed to open file for writing");
                                        free(buf);
                                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
                                        return ESP_FAIL;
                                }

                                // Find where actual file content starts
                                char *content = find_content_start(buf, ret);
                                if (content) {
                                        int content_len = ret - (content - buf);
                                        // Write initial content (may contain trailing boundary on small files)
                                        fwrite(content, 1, content_len, fp);
                                }
                                header_parsed = 1;
                        }
                } else if (fp) {
                        // Write file data
                        fwrite(buf, 1, ret, fp);
                }
        }

        if (fp) {
                fclose(fp);

                // Trim trailing boundary from file
                // Re-open and truncate the trailing boundary bytes
                struct stat st;
                if (stat(filepath, &st) == 0 && st.st_size > boundary_len + 6) {
                        // Read last bytes and check for boundary
                        fp = fopen(filepath, "r+b");
                        if (fp) {
                                // Seek to check for boundary near end
                                fseek(fp, -(boundary_len + 10), SEEK_END);
                                char tail[150];
                                int tail_len = fread(tail, 1, sizeof(tail) - 1, fp);
                                tail[tail_len] = '\0';

                                // Find boundary and truncate there
                                char *bpos = strstr(tail, boundary);
                                if (bpos) {
                                        // Calculate new size (excluding boundary and preceding CRLF)
                                        long new_size = st.st_size - tail_len + (bpos - tail);
                                        if (new_size > 2 && bpos > tail && *(bpos-1) == '\n' && *(bpos-2) == '\r') {
                                                new_size -= 2; // Remove CRLF before boundary
                                        }
                                        fclose(fp);
                                        truncate(filepath, new_size);
                                } else {
                                        fclose(fp);
                                }
                        }
                }

                ESP_LOGI(TAG, "Upload complete: %s", filename);
        }

        free(buf);
        httpd_resp_sendstr(req, "Upload complete");
        return ESP_OK;
}

// GET /mqtt - returns current MQTT settings as JSON
static esp_err_t mqtt_get_handler(httpd_req_t *req) {
        httpd_resp_set_type(req, "application/json");

        char url[256] = {0}, username[64] = {0};
        char data_topic[MQTT_MAX_TOPIC_LEN] = {0}, program_topic[MQTT_MAX_TOPIC_LEN] = {0};
        uint16_t port = 1883;
        mqtt_config_get_broker(url, sizeof(url), &port);
        mqtt_config_get_auth(username, sizeof(username), NULL, 0);
        mqtt_config_get_topics(data_topic, sizeof(data_topic), program_topic, sizeof(program_topic));
        bool enabled = mqtt_config_get_enabled();
        bool connected = mqtt_client_is_connected();

        httpd_resp_sendstr_chunk(req, "{\"broker\":\"");
        httpd_resp_sendstr_chunk(req, url);
        httpd_resp_sendstr_chunk(req, "\",\"port\":");

        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", port);
        httpd_resp_sendstr_chunk(req, port_str);

        httpd_resp_sendstr_chunk(req, ",\"username\":\"");
        httpd_resp_sendstr_chunk(req, username);
        httpd_resp_sendstr_chunk(req, "\",\"enabled\":");
        httpd_resp_sendstr_chunk(req, enabled ? "true" : "false");
        httpd_resp_sendstr_chunk(req, ",\"connected\":");
        httpd_resp_sendstr_chunk(req, connected ? "true" : "false");
        httpd_resp_sendstr_chunk(req, ",\"data_topic\":\"");
        httpd_resp_sendstr_chunk(req, data_topic);
        httpd_resp_sendstr_chunk(req, "\",\"program_topic\":\"");
        httpd_resp_sendstr_chunk(req, program_topic);
        httpd_resp_sendstr_chunk(req, "\"}");
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
}

// POST /mqtt - save MQTT settings
static esp_err_t mqtt_post_handler(httpd_req_t *req) {
        char buf[512];
        int remaining = req->content_len;

        if (remaining >= (int)sizeof(buf)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
                return ESP_FAIL;
        }

        int received = 0;
        while (remaining > 0) {
                int ret = httpd_req_recv(req, buf + received, remaining);
                if (ret <= 0) {
                        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
                        return ESP_FAIL;
                }
                received += ret;
                remaining -= ret;
        }
        buf[received] = '\0';

        // Parse form data
        char broker[256] = {0}, username[64] = {0}, password[64] = {0};
        char data_topic[MQTT_MAX_TOPIC_LEN] = {0}, program_topic[MQTT_MAX_TOPIC_LEN] = {0};
        char port_str[16] = {0}, enabled_str[8] = {0};

        httpd_query_key_value(buf, "broker", broker, sizeof(broker));
        httpd_query_key_value(buf, "port", port_str, sizeof(port_str));
        httpd_query_key_value(buf, "username", username, sizeof(username));
        httpd_query_key_value(buf, "password", password, sizeof(password));
        httpd_query_key_value(buf, "data_topic", data_topic, sizeof(data_topic));
        httpd_query_key_value(buf, "program_topic", program_topic, sizeof(program_topic));
        httpd_query_key_value(buf, "enabled", enabled_str, sizeof(enabled_str));

        url_decode(broker);
        url_decode(username);
        url_decode(password);
        url_decode(data_topic);
        url_decode(program_topic);

        // Apply settings
        uint16_t port = atoi(port_str);
        if (port == 0) port = 1883;

        mqtt_config_set_broker(broker, port);
        mqtt_config_set_auth(username, password);
        mqtt_config_set_topics(data_topic, program_topic);
        mqtt_config_set_enabled(strcmp(enabled_str, "on") == 0 ||
                                strcmp(enabled_str, "1") == 0 ||
                                strcmp(enabled_str, "true") == 0);
        mqtt_config_save();

        // Restart MQTT client with new settings
        mqtt_client_stop();
        mqtt_client_start();

        // Redirect back to main page
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
}

#if 0
static esp_err_t settings_post_handler(httpd_req_t *req) {
        char buf[256];
        int ret, remaining = req->content_len;
        char ssid[64] = {0}, password[64] = {0};
        while (remaining > 0) {
                if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
                        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
                        return ESP_FAIL;
                }
                remaining -= ret;
                char *ssid_p = strstr(buf, "ssid=");
                if (ssid_p) sscanf(ssid_p + 5, "%63[^&]", ssid);
                char *pass_p = strstr(buf, "password=");
                if (pass_p) sscanf(pass_p + 9, "%63[^&]", password);
        }
        url_decode(ssid);
        url_decode(password);
        ESP_LOGI(TAG, "Saved AP credentials for %s",ssid);
        wifi_config_set(ssid, password);
        httpd_resp_sendstr(req, "WiFi credentials saved. Please reboot device or reconnect.");
        
        return ESP_OK;
}

// ----- /scan endpoint (returns JSON) -----
static esp_err_t scan_get_handler(httpd_req_t *req) {
        scanned_ap_info_t *list;
        size_t count;
        wifi_config_get_scan_results(&list, &count);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr_chunk(req, "[");

        for (size_t i = 0; i < count; ++i) {
                char entry[128];
                snprintf(entry, sizeof(entry),
                         "{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":\"%s\"}%s",
                         list[i].ssid, list[i].rssi,
                         wifi_config_authmode_str(list[i].authmode),
                         (i < count - 1) ? "," : ""
                         );
                httpd_resp_sendstr_chunk(req, entry);
        }
        httpd_resp_sendstr_chunk(req, "]");
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
}
#endif

void mgmt_http_server_start(void) {
        if (server) return;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.max_uri_handlers = 16;
        httpd_start(&server, &config);

        httpd_uri_t index_uri = {
                .uri = "/",
                .method = HTTP_GET,
                .handler = index_handler,
                .user_ctx = NULL
        };
        httpd_uri_t favicon_uri = {
                .uri = "/favicon.ico",
                .method = HTTP_GET,
                .handler = favicon_handler,
                .user_ctx = NULL
        };
        httpd_uri_t favicon_svg_uri = {
                .uri = "/favicon.svg",
                .method = HTTP_GET,
                .handler = favicon_handler,
                .user_ctx = NULL
        };
        httpd_uri_t files_uri = {
                .uri = "/files",
                .method = HTTP_GET,
                .handler = files_handler,
                .user_ctx = NULL
        };
        httpd_uri_t upload_uri = {
                .uri = "/",
                .method = HTTP_POST,
                .handler = upload_handler,
                .user_ctx = NULL
        };
        httpd_uri_t freespace_uri = {
                .uri = "/freespace",
                .method = HTTP_GET,
                .handler = freespace_handler,
                .user_ctx = NULL
        };
        httpd_uri_t delete_uri = {
                .uri = "/delete",
                .method = HTTP_GET,
                .handler = delete_handler,
                .user_ctx = NULL
        };
        httpd_uri_t edit_uri = {
                .uri = "/edit",
                .method = HTTP_GET,
                .handler = edit_handler,
                .user_ctx = NULL
        };
        httpd_uri_t readfile_uri = {
                .uri = "/readfile",
                .method = HTTP_GET,
                .handler = readfile_handler,
                .user_ctx = NULL
        };
        httpd_uri_t save_uri = {
                .uri = "/save",
                .method = HTTP_POST,
                .handler = save_handler,
                .user_ctx = NULL
        };
        httpd_uri_t mqtt_get_uri = {
                .uri = "/mqtt",
                .method = HTTP_GET,
                .handler = mqtt_get_handler,
                .user_ctx = NULL
        };
        httpd_uri_t mqtt_post_uri = {
                .uri = "/mqtt",
                .method = HTTP_POST,
                .handler = mqtt_post_handler,
                .user_ctx = NULL
        };
        #if 0
        httpd_uri_t settings_post_uri = {
                .uri = "/settings",
                .method = HTTP_POST,
                .handler = settings_post_handler,
                .user_ctx = NULL
        };
        httpd_uri_t scan_get_uri = {
                .uri = "/scan",
                .method = HTTP_GET,
                .handler = scan_get_handler,
                .user_ctx = NULL
        };
        httpd_uri_t hs_get_uri = {
                .uri = "/hotspot-detect.html",
                .method = HTTP_GET,
                .handler = hs_get_handler,
                .user_ctx = NULL
        };
        #endif
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &favicon_uri);
        httpd_register_uri_handler(server, &favicon_svg_uri);
        httpd_register_uri_handler(server, &files_uri);
        httpd_register_uri_handler(server, &upload_uri);
        httpd_register_uri_handler(server, &freespace_uri);
        httpd_register_uri_handler(server, &delete_uri);
        httpd_register_uri_handler(server, &edit_uri);
        httpd_register_uri_handler(server, &readfile_uri);
        httpd_register_uri_handler(server, &save_uri);
        httpd_register_uri_handler(server, &mqtt_get_uri);
        httpd_register_uri_handler(server, &mqtt_post_uri);
        //httpd_register_uri_handler(server, &settings_post_uri);
        //httpd_register_uri_handler(server, &scan_get_uri);
        //httpd_register_uri_handler(server, &hs_get_uri);
}

void mgmt_http_server_stop(void) {
        if (server) {
                httpd_stop(server);
                server = NULL;
        }
}
