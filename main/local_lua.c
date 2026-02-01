
#include <lauxlib.h>
#include <lualib.h>
#include <ctype.h>
#include <lua.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "local_lua.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "luafuncs.h"

static const char* TAG = "lua";

extern bool force_exit;

// Error display colors
#define ERR_TITLE_R 255
#define ERR_TITLE_G 0
#define ERR_TITLE_B 0

// Yellow for error message text
#define ERR_MSG_R 255
#define ERR_MSG_G 255
#define ERR_MSG_B 0

// Strip unprintable characters and replace file path with "line X"
// Returns pointer to static buffer - not thread safe
static const char *sanitize_error_msg(const char *input) {
    static char sanitized[512];
    int out_pos = 0;

    if (input == NULL) return NULL;

    // Check for /assets/display.lua:<number>: pattern and replace with "line <number>:"
    const char *file_prefix = "/assets/display.lua:";
    const char *match = strstr(input, file_prefix);
    if (match) {
        // Skip to after the file path
        const char *after_prefix = match + strlen(file_prefix);
        // Find where the line number ends (next colon)
        const char *line_end = strchr(after_prefix, ':');
        if (line_end) {
            // Copy "line " prefix
            const char *line_str = "line ";
            while (*line_str && out_pos < (int)sizeof(sanitized) - 1) {
                sanitized[out_pos++] = *line_str++;
            }
            // Copy the line number
            while (after_prefix < line_end && out_pos < (int)sizeof(sanitized) - 1) {
                if (isprint((int)*after_prefix)) {
                    sanitized[out_pos++] = *after_prefix;
                }
                after_prefix++;
            }
            // Skip past the colon and any space
            input = line_end + 1;
            while (*input == ' ') input++;
            // Add separator
            if (out_pos < (int)sizeof(sanitized) - 2) {
                sanitized[out_pos++] = ':';
                sanitized[out_pos++] = ' ';
            }
        }
    }

    // Copy rest of string, filtering unprintable characters
    for (int i = 0; input[i] && out_pos < (int)sizeof(sanitized) - 1; i++) {
        if (isprint((int)input[i])) {
            sanitized[out_pos++] = input[i];
        }
    }
    sanitized[out_pos] = '\0';
    return sanitized;
}

// Display a Lua error on the LED panel
// Wraps long error messages across multiple lines
static void show_lua_error(const char *error_msg) {
    clear_display();

    // Show "ERROR" title in red using 8x8 font
    draw_text("LUA ERROR", 2, 0, ERR_TITLE_R, ERR_TITLE_G, ERR_TITLE_B, 8);

    if (error_msg == NULL) {
        draw_text("Unknown error", 2, 12, ERR_MSG_R, ERR_MSG_G, ERR_MSG_B, 5);
        return;
    }

    // Sanitize the error message - strips unprintable chars and replaces file path with "line X"
    const char *msg = sanitize_error_msg(error_msg);
    if (msg == NULL || msg[0] == '\0') {
        draw_text("Unknown error", 2, 12, ERR_MSG_R, ERR_MSG_G, ERR_MSG_B, 5);
        return;
    }

    // Display error message with word wrapping using 5x5 font
    // 5x5 font is 6 pixels wide per char
    int display_width = get_width();
    int display_height = get_height();
    int x_offset = 2;  // Where we start drawing text
    int chars_per_line = (display_width - x_offset) / 6;
    if (chars_per_line < 6) chars_per_line = 6;

    int y = 10;           // Start right below title (8x8 font + 2 spacing)
    int line_height = 7;  // 5 pixels + 2 spacing for 5x5 font
    int max_lines = (display_height - y) / line_height;
    if (max_lines < 1) max_lines = 1;
    int line_count = 0;

    char line_buf[64];
    int msg_len = strlen(msg);
    int pos = 0;

    while (pos < msg_len && line_count < max_lines) {
        // Copy up to chars_per_line characters
        int copy_len = msg_len - pos;
        if (copy_len > chars_per_line) copy_len = chars_per_line;
        if (copy_len > (int)sizeof(line_buf) - 1) copy_len = sizeof(line_buf) - 1;

        strncpy(line_buf, msg + pos, copy_len);
        line_buf[copy_len] = '\0';

        draw_text(line_buf, x_offset, y, ERR_MSG_R, ERR_MSG_G, ERR_MSG_B, 5);

        y += line_height;
        pos += copy_len;
        line_count++;
    }
}

// Function to log memory usage with the message at the end
void log_memory_usage(const char* message)
{
    ESP_LOGI(TAG, "Free heap: %d, Min free heap: %d, Largest free block: %d, %s",
        heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
        heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
        heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
        message);
}

int lua_panic_func(lua_State *L) {
    ESP_LOGI(TAG, "LUA PANIC");
    return 0;
}

// lua vm debug callback to avoid watchdog bites
static void debug_hook(lua_State *LUA, lua_Debug *dbg){
	(void)dbg;

    // Log memory usage every 5 seconds
    static int64_t last_log_time = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_log_time >= 5000000) {  // 5 seconds in microseconds
        last_log_time = now;
        log_memory_usage("DBG");
    }

    if(force_exit) {
        force_exit = false;
        lua_pushstring(LUA, "LUA Restarting...");
        lua_error(LUA);
    }
}



lua_State * lua_init(void){

    lua_State* LUA = luaL_newstate();

    if (LUA == NULL) {
        ESP_LOGE(TAG, "Failed to create new Lua state");
        return NULL;
    }
    log_memory_usage("After luaL_newstate");

    lua_atpanic( LUA, lua_panic_func);

    luaL_openlibs(LUA);
    
    load_lua_funcs(LUA);

	// setup debug hook callback
	lua_sethook(LUA, debug_hook, LUA_MASKCOUNT, 1000);
    
    // Set the Lua module search path to include the assets directory
    if (luaL_dostring(LUA, "package.path = package.path .. ';./?.lua;/assets/?.lua'")) {
        ESP_LOGE(TAG, "Failed to set package.path: %s", lua_tostring(LUA, -1));
        lua_pop(LUA, 1); // Remove error message from the stack
    }

    return LUA;

}

// Function to run a Lua script from file
void run_lua_file(const char* file_name)
{
    ESP_LOGI(TAG, "Running: %s", file_name);

    log_memory_usage("Start of test");

    lua_State* L = lua_init();
    if (L == NULL) {
        show_lua_error("Failed to create Lua state (out of memory?)");
        vTaskDelay(pdMS_TO_TICKS(3000));
        return;
    }

    if (luaL_dostring(L, "clear_display()") == LUA_OK) {
        lua_pop(L, lua_gettop(L));
    } else {
        const char *err_msg = lua_tostring(L, -1);
        ESP_LOGE(TAG, "Error running embedded Lua script: %s", err_msg);
        show_lua_error(err_msg);
        lua_close(L);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return;
    }

    // Construct the full file path
    char full_path[128];
    snprintf(full_path, sizeof(full_path), LUA_FILE_PATH "/%s", file_name);

    if (luaL_dofile(L, full_path) == LUA_OK) {
        lua_pop(L, lua_gettop(L));
    } else {
        const char *err_msg = lua_tostring(L, -1);
        ESP_LOGE(TAG, "Error running Lua script from file '%s': %s", full_path, err_msg);

        // Show error on the LED panel
        show_lua_error(err_msg);

        lua_pop(L, 1); // Remove error message from the stack

        // Keep error displayed for a while before continuing
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    log_memory_usage("After executing Lua script from file");

    lua_close(L);
    log_memory_usage("After lua_close");

    ESP_LOGI(TAG, "End of %s", file_name);
}

// Function to run an embedded Lua script
void run_lua_string(const char* lua_script, const char* test_name)
{
    ESP_LOGI(TAG, "Starting Lua test: %s", test_name);

    log_memory_usage("Start of test");

    lua_State* L = luaL_newstate();
    if (L == NULL) {
        ESP_LOGE(TAG, "Failed to create new Lua state");
        show_lua_error("Failed to create Lua state (out of memory?)");
        vTaskDelay(pdMS_TO_TICKS(3000));
        return;
    }
    log_memory_usage("After luaL_newstate");

    luaL_openlibs(L);
    log_memory_usage("After luaL_openlibs");

    if (luaL_dostring(L, lua_script) == LUA_OK) {
        lua_pop(L, lua_gettop(L));
    } else {
        const char *err_msg = lua_tostring(L, -1);
        ESP_LOGE(TAG, "Error running embedded Lua script: %s", err_msg);

        // Show error on the LED panel
        show_lua_error(err_msg);

        lua_pop(L, 1); // Remove error message from the stack

        // Keep error displayed for a while
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    log_memory_usage("After executing Lua script");

    lua_close(L);
    log_memory_usage("After lua_close");

    ESP_LOGI(TAG, "End of Lua test: %s", test_name);
}
