idf_component_register(SRCS "luamatrix.c" "display.cpp" "local_lua.c" "luafuncs.c" "mgmt_http_server.c"
                    INCLUDE_DIRS "../include" )

target_add_binary_data(${COMPONENT_TARGET} "templates/favicon.svg" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "templates/mgmt_index.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "templates/logout.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "templates/edit.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "templates/firmware.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "templates/reboot.html" TEXT)
