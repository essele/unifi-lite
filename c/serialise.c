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

/*==============================================================================
 * Utility function to handle a growing string buffer, this is similar to the
 * Lua_Buffer concept, but should be quicker where we don't need to convert back
 * to a lua variable
 *==============================================================================
 */
#define BUF_INC			2048
#define MAXNUMLEN		32

struct charbuf {
	char	*p;
	int		alloc;
	int free;
};

struct charbuf *charbuf_new() {
	struct charbuf *b = malloc(sizeof(struct charbuf));
	b->p = malloc(BUF_INC);
	b->alloc = b->free = BUF_INC;
	return b;
}
void charbuf_free(struct charbuf *b) {
	free(b->p);
	free(b);
}
void inline charbuf_make_space(struct charbuf *b, int need) {
	// TODO: do this in one go, rather than a loop
	while(b->free < need) {
		b->alloc += BUF_INC; b->free += BUF_INC;
		b->p = realloc(b->p, b->alloc);
	}
}
void charbuf_addstring(struct charbuf *b, char *d, int len) {
	if(!len) len = strlen(d);
	charbuf_make_space(b, len);
	memcpy(b->p+(b->alloc-b->free), d, len);
	b->free -= len;
}
void charbuf_addchar(struct charbuf *b, char c) {
	charbuf_make_space(b, 1);
	b->p[b->alloc-b->free] = c;
	b->free--;
}
void charbuf_addchars(struct charbuf *b, char c, int len) {
	while(len--) charbuf_addchar(b, c);
}
void charbuf_addnumber(struct charbuf *b, double num) {
	charbuf_make_space(b, MAXNUMLEN);
	b->free -= sprintf(&b->p[b->alloc-b->free], "%.14g", num);
}
/* Note, the string will need freeing */
char *charbuf_tostring(struct charbuf *b, int *len) {
	char	*p = b->p;

	if(!len)
		charbuf_addchar(b, '\0');
	else
		*len = b->alloc-b->free;
	free(b);
	return p;
}

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
		lua_newtable(L);
		REMOVE_SPACE(p);
		while(*p != '}') {
			// keys are always strings so we can use this routing...
			if(!unserialise_variable(L, &p)) goto err1;
			REMOVE_SPACE(p);
			if(*p++ != ':') goto err1;
			REMOVE_SPACE(p);
			if(!unserialise_variable(L, &p)) goto err1;
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
		lua_newtable(L);
		lua_pushstring(L, "__list");
		lua_pushnumber(L, 1);
		lua_rawset(L, -3);
		// TODO: set ref to tell us we are a list
		while(*p != ']') {
			lua_pushnumber(L, i++);
			REMOVE_SPACE(p);
			if(!unserialise_variable(L, &p)) goto err2;
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
	} if(strncmp(p, "null", 4) == 0) {
		*str = p+4;
		lua_pushnil(L);
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

/*==============================================================================
 * Given an index into the lua stack (i.e. a variable) we will serialise the
 * data into a form that we can either store in a file or send over the network
 *
 * We use charbuf to concat the string in a reasonably efficient way
 *==============================================================================
 */
#define INDENTING			(indent>-1)
#define INDENT(b)			if(indent>-1) charbuf_addchars(b, '\t', indent)

int serialise_variable(lua_State *L, int index, struct charbuf *b, int indent) {
	const char	*p;
	size_t		len;
	int			type = lua_type(L, index);
	int			first = 1;
	int			i = 1;

	switch(type) {
	case LUA_TBOOLEAN:
		charbuf_addstring(b, (lua_toboolean(L, index) ? "true" : "false"), 0);
		break;
	case LUA_TNUMBER:
		charbuf_addnumber(b, lua_tonumber(L, index));
		break;
	case LUA_TSTRING:
		charbuf_addchar(b, '"');
		// We need to quote quotes...
		p = lua_tolstring(L, index, &len);
		while(len--) {
			switch(*p) {
			case '\"':
			case '\\':
			case '\n':
				// For this use case we keep \n as it is
				// charbuf_addchar(b, '\\');
				charbuf_addchar(b, *p++);
				if(INDENTING) charbuf_addchars(b, '\t', 5);
				break;
			case '\0':
				charbuf_addstring(b, "\\000", 4);
				p++;
				break;
			default:
				charbuf_addchar(b, *p++);
				break;
			}
		}
		charbuf_addchar(b, '"');
		break;
	case LUA_TLIGHTUSERDATA:
		// this isn't relevant to keeping
		charbuf_addstring(b, "null", 4);
		break;
	case LUA_TTABLE:
		// we could be a hash, or we could be a list (look for __list)
		lua_getfield(L, index, "__list");
		if(lua_isnil(L, -1)) {
			// We are a normal hash...
			lua_pop(L, 1);
			charbuf_addchar(b, '{');
			if(INDENTING) { charbuf_addchar(b, '\n'); indent ++; }

			lua_pushnil(L);
			if(index < 0) index--;		// allow for the extra pushnil if we are negative
			while(lua_next(L, index) != 0) {
				if(first)
					first = 0;
				else
					(INDENTING ? charbuf_addstring(b, " ,\n", 3) : charbuf_addchar(b, ','));
	
				INDENT(b);	
				serialise_variable(L, -2, b, 0);
				(INDENTING ? charbuf_addstring(b, " : ", 3) : charbuf_addchar(b, ':'));
				serialise_variable(L, -1, b, indent);
				lua_pop(L, 1);
			}
			if(INDENTING) { charbuf_addchar(b, '\n'); indent--; INDENT(b); }
			charbuf_addchar(b, '}');
		} else {
			// We are an indexed list...
			lua_pop(L, 1);

			charbuf_addchar(b, '[');
			if(INDENTING) { charbuf_addchar(b, '\n'); indent++; }

			while(1) {
				lua_rawgeti(L, index, i++);
				if(lua_isnil(L, -1)) {
					lua_pop(L, 1);
					break;
				}
				if(first)
					first = 0;
				else
					(INDENTING ? charbuf_addstring(b, " ,\n", 3) : charbuf_addchar(b, ','));

				INDENT(b);
				serialise_variable(L, -1, b, indent);
				lua_pop(L, 1);
			}
			if(INDENTING) { charbuf_addchar(b, '\n'); indent--; INDENT(b); }
			charbuf_addchar(b, ']');
		}
		break;
	}
	return 0;
}


int unserialise(lua_State *L) {
	char *p = 0;

	return unserialise_variable(L, &p);
}
    
int serialise(lua_State *L) {
	struct charbuf 	*b = charbuf_new();
	char			*s;
	int				len;

	serialise_variable(L, 1, b, 0);
	s = charbuf_tostring(b, &len);
	lua_pushlstring(L, s, len);
	free(s);
	return 1;
}

