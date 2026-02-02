
#pragma once

#define LUA_FILE_PATH "/assets"

void run_lua_file(const char* file_name);

// Request Lua to pause execution briefly to free up CPU/memory for HTTP
// duration_ms: how long to pause (0 to clear the request)
void lua_request_pause(int duration_ms);
