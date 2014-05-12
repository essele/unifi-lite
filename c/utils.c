/*------------------------------------------------------------------------------
 *  This file is part of UniFi-lite.
 *  Copyright (C) 2014 Lee Essen <lee.essen@nowonline.co.uk>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *------------------------------------------------------------------------------
 */
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <errno.h>
#include <time.h>

/*==============================================================================
 * Simple function to get the time in milliseconds (we return a string since
 * thats what unifi seems to want to use)
 *==============================================================================
 */
int get_time(lua_State *L) {
	struct timespec	 	spec;
	char				buf[24];

	clock_gettime(CLOCK_REALTIME, &spec);
	sprintf(buf, "%ld%03ld", spec.tv_sec, spec.tv_nsec/1000000);
	lua_pushstring(L, buf);
	return 1;
}

/*==============================================================================
 * Given a table, add a string (using newline terminators) to a given element.
 * This is mostly used for the mgmtcfg field which is a horrible newline
 * separated list
 *
 * add_cfg(table, field, item, value)
 *==============================================================================
 */
int add_cfg(lua_State *L) {
	luaL_Buffer	b;

	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TSTRING);
	luaL_checktype(L, 3, LUA_TSTRING);
	luaL_checktype(L, 4, LUA_TSTRING);

	luaL_buffinit(L, &b);

	// Get the current value of the field...
	lua_pushvalue(L, 2);
	lua_gettable(L, 1);

	// If it's a string then put it in the buffer with a \n on the end
	if(lua_isstring(L, -1)) {
		luaL_addvalue(&b);
		luaL_addchar(&b, '\n');
	} else 
		lua_pop(L, 1);

	// Now create the <item>=<value> string
	lua_pushvalue(L, 3);
	luaL_addvalue(&b);
	luaL_addchar(&b, '=');
	lua_pushvalue(L, 4);
	luaL_addvalue(&b);

	// Put it back into the table...
	lua_pushvalue(L, 2);
	luaL_pushresult(&b);
	lua_settable(L, 1);
	return 0;
}

