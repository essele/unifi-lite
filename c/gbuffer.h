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
 * gbuffer_need(&b, <size>)				-- make space for at least <size>
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

#define gbuffer_cur(b) 				(b->p + b->data_size)
#define gbuffer_ptr(b) 				(b->p)
#define gbuffer_inc(b, len) 		b->data_size += len
#define gbuffer_size(b)				(b->data_size)
#define gbuffer_reset(b)			b->data_size = 0
#define MAXNUMLEN       			32

static inline void gbuffer_need(struct gbuffer *b, size_t reqd) {
	if((b->alloc_size - b->data_size) < reqd) {
		size_t need = ((reqd/b->blk_size)+1) * b->blk_size;

		b->alloc_size += need;
		b->p = realloc(b->p, b->alloc_size);
	}
}
static inline struct gbuffer *gbuffer_new(size_t blksize) {
	struct gbuffer *b = malloc(sizeof(struct gbuffer));
	if(!b) return NULL;

	b->p = NULL;
	b->alloc_size = 0;
	b->data_size = 0;
	b->blk_size = blksize;
	
	return b;
}
static inline void gbuffer_free(struct gbuffer *b) {
	if(b->p) free(b->p);
	free(b);
}
static inline void gbuffer_addchar(struct gbuffer *b, char c) {
	gbuffer_need(b, 1);
	b->p[b->data_size++] = c;
}
static inline void gbuffer_addchars(struct gbuffer *b, char c, size_t len) {
	gbuffer_need(b, len);
	while(len--) b->p[b->data_size++] = c;
}

// Note that this returns the length without the trailing zero
static inline char *gbuffer_tostring(struct gbuffer *b, size_t *len) {
	*len = b->data_size;
	gbuffer_addchar(b, '\0');
	return(b->p);
}
static inline void gbuffer_addstring(struct gbuffer *b, char *p, size_t len) {
	if(!len) len = strlen(p);
	gbuffer_need(b, len);
	memcpy(b->p+b->data_size, p, len);
	b->data_size += len;
}
static inline void gbuffer_addquoted(struct gbuffer *b, char *p, size_t len) {
	char 		*d = b->p + b->data_size;
	char 		*s = d;
	size_t		i = 0;

	if(!len) len = strlen(p);
	gbuffer_need(b, len*2);		// worst case is all quoted!
	while(i < len) {
		switch(*p) {
			case '"':
				*d++ = '\\'; 
				*d++ = *p++;
				break;
			case '\n':
				*d++ = '\\'; 
				*d++ = 'n'; 
				p++;
				break;
			default:
				*d++ = *p++;
		}
		i++;
	}
	b->data_size += (d-s);
}
static inline void gbuffer_addnumber(struct gbuffer *b, double num) {
    gbuffer_need(b, MAXNUMLEN);
    b->data_size += sprintf(b->p+b->data_size, "%.14g", num);
}
static inline size_t gbuffer_read(struct gbuffer *b, int fd, size_t count) {
	size_t	rc;
	gbuffer_need(b, count);
	rc = read(fd, gbuffer_cur(b), count);
	if(rc > 0) gbuffer_inc(b, rc);
	return rc;
}
static inline void gbuffer_remove(struct gbuffer *b, size_t start, size_t count) {
	b->data_size -= count;
	memmove(b->p + start, b->p + (start+count), (b->data_size - start));
}

#endif

