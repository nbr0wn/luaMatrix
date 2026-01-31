
#include <lauxlib.h>
#include <lualib.h>
#include <lua.h>
#include "esp_log.h"
#include "local_lua.h"
#include "esp_heap_caps.h"
#include "display.h"
#include "luafuncs.h"

static const char* TAG = "lua";

extern bool force_exit;

int lua_panic_func(lua_State *L) {
    ESP_LOGI(TAG, "LUA PANIC");
    return 0;
}

// lua vm debug callback to avoid watchdog bites
static void debug_hook(lua_State *LUA, lua_Debug *dbg){
	(void)dbg;
    if(force_exit) {
        force_exit = false;
        lua_pushliteral(LUA, "Force Exit");
        lua_error(LUA);
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

    if (luaL_dostring(L, "clear_display()") == LUA_OK) {
        lua_pop(L, lua_gettop(L));
    } else {
        ESP_LOGE(TAG, "Error running embedded Lua script: %s", lua_tostring(L, -1));
        lua_close(L);
        return;
    }

    // Construct the full file path
    char full_path[128];
    snprintf(full_path, sizeof(full_path), LUA_FILE_PATH "/%s", file_name);

    if (luaL_dofile(L, full_path) == LUA_OK) {
        lua_pop(L, lua_gettop(L));
    } else {
        ESP_LOGE(TAG, "Error running Lua script from file '%s': %s", full_path, lua_tostring(L, -1));
        lua_pop(L, 1); // Remove error message from the stack
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
        return;
    }
    log_memory_usage("After luaL_newstate");

    luaL_openlibs(L);
    log_memory_usage("After luaL_openlibs");

    if (luaL_dostring(L, lua_script) == LUA_OK) {
        lua_pop(L, lua_gettop(L));
    } else {
        ESP_LOGE(TAG, "Error running embedded Lua script: %s", lua_tostring(L, -1));
        lua_pop(L, 1); // Remove error message from the stack
    }
    log_memory_usage("After executing Lua script");

    lua_close(L);
    log_memory_usage("After lua_close");

    ESP_LOGI(TAG, "End of Lua test: %s", test_name);
}
