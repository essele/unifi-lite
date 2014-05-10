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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "serialise.h"

/*------------------------------------------------------------------------------
 * Push a copy of the string without the escapes (remove single backslashes)
 * (len needs to be the size of the resultant string, so we must know in
 * advance)
 *------------------------------------------------------------------------------
 */
void lua_pushcleanlstring(lua_State *L, char *p, int len) {
	char *s = malloc(len);
	char *d = s;
	int i;
	
	for(i=0; i<len; i++) {
		if(*p == '\\') p++;
		*d++ = *p++;
	}
	lua_pushlstring(L, s, len);
	free(s);
}

/*==============================================================================
 * This takes a text stream and turns it into a table structure
 *==============================================================================
 */
#define REMOVE_SPACE(p) while(isspace(*p)) p++
int unserialise_variable(lua_State *L, char **str) {
	char		*p = *str;
	char		*e;
	int 		len = 0;
	int			i = 1;
	int			copyflag = 0;
	lua_Number	num;

	fprintf(stderr, "unserialise_variable called with: %s\n", p);
	
	REMOVE_SPACE(p);	// remove leading whitespace
	if(*p == '"') {
		e = (char *)++p;
		while(*e != '"') {
			if(!*e) goto err;
			if(*e == '\\') { e++; copyflag=1; }
			e++; len++;
		}
		// now we have worked out how long the string is ... we copy if we have to edit...
		(copyflag ? lua_pushcleanlstring(L, p, len) : lua_pushlstring(L, p, len));
		*str=++e;
		return 1;
	} else if(*p == '{') {
		// We are a table, we should have key value pairs...
		p++;
		fprintf(stderr, "newtable\n");
		lua_newtable(L);
		REMOVE_SPACE(p);
		while(*p != '}') {
			// keys are always strings so we can use this routing...
			if(!unserialise_variable(L, &p)) goto err1;
		fprintf(stderr, "have key\n");
			REMOVE_SPACE(p);
			if(*p++ != ':') goto err1;
		fprintf(stderr, "have colon\n");
			REMOVE_SPACE(p);
			if(!unserialise_variable(L, &p)) goto err1;
		fprintf(stderr, "have value\n");
			lua_rawset(L, -3);
			REMOVE_SPACE(p);
			if(*p != ',') break;
			p++;
		}
		REMOVE_SPACE(p);
		if(*p != '}') { lua_pop(L, 1); *str = 0; return 0; }
		*str = ++p;
		return 1;
	} else if(*p == '[') {
		// We are a list...
		p++;
		fprintf(stderr, "new list\n");
		lua_newtable(L);
		while(*p != ']') {
			fprintf(stderr, "index %d\n", i);
			lua_pushnumber(L, i++);
			REMOVE_SPACE(p);
			if(!unserialise_variable(L, &p)) goto err2;
			fprintf(stderr, "have value\n");
			lua_rawset(L, -3);
			REMOVE_SPACE(p);
			if(*p != ',') break;
			p++;
		}
		REMOVE_SPACE(p);
		if(*p != ']') goto err1;
		*str = ++p;
		return 1;
	} else if(*p == 't' || *p == 'f') {
		if(strncmp(p, "true", 4) == 0) {
			lua_pushboolean(L, 1);
			*str = p+4;
		} else if(strncmp(p, "false", 5) == 0) {
			lua_pushboolean(L, 0);
			*str = p+5;
		}
		return 1;
	} else {
		// must be a number...
		len = strspn(p, "0123456789.-");
		if(!len) goto err;
		lua_pushlstring(L, p, len);
		num = lua_tonumber(L, -1);
		lua_pop(L, 1);
		lua_pushnumber(L, num);
		*str = p+len;
		return 1;
	}
	return 0;

err2:	lua_pop(L, 1);
err1:	lua_pop(L, 1);
err:	*str = 0;
		return 0;
}


int unserialise(lua_State *L) {
	char *p = 0;

	return unserialise_variable(L, &p);
}
    
int serialise(lua_State *L) {
	return 0;
}

