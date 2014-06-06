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

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "gbuffer.h"
#include "log.h"

/*==============================================================================
 * Given a template filename, we will open the file run through and create
 * lua code to reproduce the template. We will specifically look for code
 * embedded within [[ and ]] and keep that as bare code.
 *
 * If the first char of bare code is a $ then we assume "_op()" around it.
 * If the ]] is on a line all on it's own then we ditch the trailing \n.
 *
 * _op is a custom function which will "output" the result, this will probably
 * just be something that concatenates a string.
 *==============================================================================
 */
int process_template(char *filename, struct gbuffer *b) {
	char			*src;
	char			*p;
	size_t			len;
	int				fd;
	
	if((fd = open(filename, O_RDONLY)) < 0) {
		wlog(LOG_ERROR, "%s: unable to open template file(%s): %s", __func__, filename, strerror(errno));
		return 0;
	}
	// work out length of file
	len = (size_t)lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	// Allocate memory and read it in...
	src = malloc(len+1);
	if(!src) {
		wlog(LOG_ERROR, "%s: unable to malloc for template(%s): %s", __func__, filename, strerror(errno));
		close(fd);
		return 0;
	}
	if(read(fd, src, len) != len) {
		wlog(LOG_ERROR, "%s: unable to read template(%s): %s", __func__, filename, strerror(errno));
		close(fd);
		return 0;
	}
	close(fd);
	src[len] = '\0';

	// p will be the start each time...
	p = src;

	while(1) {
		char	*sc = strstr(p, "[[");
		if(!sc) {
			// no code left, so we just write the rest...
			gbuffer_addstring(b, "_op = _op .. \"", 0);
			gbuffer_addquoted(b, p, 0);
			gbuffer_addstring(b, "\"\n", 0);
			break;
		} else {
			// we have some code, so we write the text and then the code
			// within the code we check for the $ as first char
			char *ec = strstr(sc+2, "]]");
			int skipnl = 0;

			if(!ec) {
				wlog(LOG_ERROR, "%s: corrupt template file: %s", __func__, filename);
				return 0;
			}
			if((sc == src || *(sc-1) == '\n') && *(ec+2) == '\n') {
				// we are the only code on the line... so we want to skip the end newline
				skipnl = 1;
			}
			// First the text before the code...
			if(sc > src) {
				gbuffer_addstring(b, "_op = _op .. \"", 0);
				gbuffer_addquoted(b, p, (sc-p));
				gbuffer_addstring(b, "\"\n", 0);
			}
			// Then the code (take care of $)...
			sc += 2;
			if(*sc == '$') {
				sc++;
				gbuffer_addstring(b, "_op = _op .. ", 0);
				gbuffer_addstring(b, sc, (ec-sc));
				gbuffer_addchar(b, '\n');
			} else {
				gbuffer_addstring(b, sc, (ec-sc));
				gbuffer_addchar(b, '\n');
			}
			p = ec + 2 + skipnl;
		}
	}	
	free(src);
	return 1;
}


int template(lua_State *L) {
	char			*fname, *p;
	size_t			len;
	struct gbuffer	*b = gbuffer_new(8192);

	luaL_checktype(L, 1, LUA_TSTRING);
	luaL_checktype(L, 2, LUA_TTABLE);
	fname = (char *)lua_tostring(L, 1);

	if(!process_template(fname, b)) {
		fprintf(stderr, "TEMPLATE FAIL\n");
		gbuffer_free(b);
		return 0;
	}
	gbuffer_addstring(b, "return _op", 0);
	p = gbuffer_tostring(b, &len);
	fprintf(stderr, "CODE:\n%s", p);

	int xx = luaL_loadbuffer(L, p, len, "template");
	fprintf(stderr, "load  xx=%d\n", xx);
	gbuffer_free(b);

	// Push the provided table for the new environment... but set
	// the global _op first...
	lua_pushvalue(L, 2);
	lua_pushstring(L, "_op");
	lua_pushstring(L, "");
	lua_settable(L, -3);

	// Create a new metatable... with to reference the
	// globals tables as a __index.
	lua_newtable(L);
	lua_pushstring(L, "__index");
	lua_getglobal(L, "_G");	
	lua_settable(L, -3);

	// Set the metatable and the environment...
	lua_setmetatable(L, -2);
	lua_setfenv(L, -2);
	
	// We expect a single return value (the output)
	if(lua_pcall(L, 0, 1, 0) != 0) {
		// we have an error, it will be on the stack
		return 1;
	}
	fprintf(stderr, "pcall  xx=%d\n", xx);
	return 1;
}

