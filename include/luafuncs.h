
#pragma once

// Forward declaration to avoid pulling in lua.h everywhere
struct lua_State;

void load_lua_funcs(struct lua_State *LUA);

// C-callable text drawing function
// size: 3, 5, 8, or 16 (font pixel height)
void draw_text(const char *str, int x, int y, int r, int g, int b, int size);