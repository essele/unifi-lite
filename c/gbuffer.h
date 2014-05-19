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

/*
 * This is a simple "growing buffer" construct that allows for a buffer
 * that can be added to and will auto grow.
 *
 * struct gbuffer		b;
 *
 * gbuffer_init(&b, <blocksize>)		-- blocksize is the alloc increment
 * gbuffer_free(&b)						-- free the buffer
 * gbuffer_add*							-- add stuff to the buffer
 *
 */

#ifndef _GBUFFER_H
#define _GBUFFER_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


struct gbuffer {
	char	*p;
	size_t	alloc_size;
	size_t	data_size;
	size_t	blk_size;
};

static inline void make_space(struct gbuffer *b, size_t reqd) {
	if((b->alloc_size - b->data_size) < reqd) {
		size_t need = ((reqd/b->blk_size)+1) * b->blk_size;

		b->alloc_size += need;
		b->p = realloc(b->p, b->alloc_size);
	}
}
static inline void gbuffer_init(struct gbuffer *b, size_t blksize) {
	b->p = NULL;
	b->alloc_size = 0;
	b->data_size = 0;
	b->blk_size = blksize;
}
static inline void gbuffer_free(struct gbuffer *b) {
	if(b->p) free(b->p);
}
static inline void gbuffer_add_char(struct gbuffer *b, char c) {
	make_space(b, 1);
	b->p[b->data_size++] = c;
}
static inline void gbuffer_add_lstring(struct gbuffer *b, char *p, size_t len) {
	make_space(b, len);
	memcpy(b->p+b->data_size, p, len);
	b->data_size += len;
}
static inline void gbuffer_add_string(struct gbuffer *b, char *p) {
	int len = strlen(p);
	gbuffer_add_lstring(b, p, len);
}
static inline void gbuffer_add_lquoted(struct gbuffer *b, char *p, size_t len) {
	char 		*s = p;
	char 		*d = b->p + b->data_size;
	size_t		i = 0;
	
	make_space(b, len*2);		// worst case is all quoted!
	while(i < len) {
		switch(*s) {
			case '"':
				*d++ = '\\'; 
				*d++ = *s++;
				break;
			case '\n':
				*d++ = '\\'; 
				*d++ = 'n'; 
				s++;
				break;
			default:
				*d++ = *s++;
		}
		i++;
	}
	b->data_size += (s-p);
}
static inline void gbuffer_add_quoted(struct gbuffer *b, char *p) {
	int len = strlen(p);
	gbuffer_add_lquoted(b, p, len);
}

#endif

