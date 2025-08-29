#pragma once

#include "lua.h"
#include "lualib.h"

// open the library as a standard global luau library
int luaopen_socket(lua_State* L);
// open the library as a table on top of the stack
int luteopen_socket(lua_State* L);

namespace sock
{

int socket_connect(lua_State* L);
int socket_close(lua_State* L);
int socket_send(lua_State* L);
int socket_recv(lua_State* L);

static const luaL_Reg lib[] = {
	{"connect", socket_connect},
	{"close", socket_close},
	{"send", socket_send},
	{"recv", socket_recv},
	{nullptr, nullptr},
};

}
