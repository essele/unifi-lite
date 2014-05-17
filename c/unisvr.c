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
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include "serialise.h"
#include "utils.h"

/*==============================================================================
 * Globals (local to this module)
 *==============================================================================
 */
#define BACKLOG			128						// max outstanding connections
#define LOG_FILE		"./x.log"				// for logging issues

#define LOG_FATAL		0
#define LOG_ERROR		1
#define LOG_WARN		2
#define LOG_INFO		3
#define LOG_DEBUG		4

static char				HTTP_SEP[5] = { 0x0d, 0x0a, 0x0d, 0x0a, 0x00 };
static char				INFORM_MAGIC[4] = { 'T', 'N', 'B', 'U' };
static int				http_fd;				// socket for http listener
static FILE				*logf;					// fh for log file
static int				log_level = LOG_INFO;	// default logging level

/*
 * The header structure for an "inform" message
 */
struct inform {
	char			magic[4];
	uint32_t		version;
	uint8_t			mac[6];
	uint16_t		flags;
	uint8_t			iv[16];
	uint32_t		data_version;
	uint32_t		data_length;
//	uint8_t			data[1];
};

/*
 * We will maintain a list of clients with current state as to how much data
 * we need to receive or send
 */
struct client {
	struct client	*next;

	int				fd;				// socket for client
	int				state;			// 0 = waiting for \n\r\n\r
									// 1 = waiting for data (recv)
									// 2 = ready to give to lua
									// 3 = idle (with lua)
									// 4 = waiting to write (send)
	int				expected;		// how much to send/receive
	int				complete;		// how much we have sent/received
	char			*buffer;		// our data buffer
	int				b_size;			// the buffer alloc'd size

	// Saved fields for reference
	int				status;			// for replying
	uint32_t		data_length;
	uint8_t			*data;
	uint8_t			mac[6];
	uint8_t			iv[16];
};

struct client 		*clients;

/*==============================================================================
 * Write stuff into our log file, we have a level and only write stuff that is
 * less than or equal the level
 *==============================================================================
 */
char	*log_levels[5] = { "FATAL:", "ERROR:", "WARN:", "INFO:", "DEBUG:" };
static void wlog(int level, char *fmt, ...) {
	va_list 	ap;
	char		tstr[30];
	time_t		t;
	struct tm	*tmp;

	// Sanity on the log_level...	
	if(level > log_level) return;
	if(level < 0 || level > 4) level = LOG_ERROR;

	// Inclue the time format...
	t = time(NULL);
	tmp = localtime(&t);
	if(tmp == NULL) {
		sprintf(tstr, "--unknown--");
	} else {
		strftime(tstr, sizeof(tstr), "%b %d %T", tmp);
	}

	// Output...
	va_start(ap, fmt);
	fprintf(logf, "%s  %-6.6s ", tstr, log_levels[level]);
	vfprintf(logf, fmt, ap);
	fprintf(logf, "\n");
	fflush(logf);
}
 
/*==============================================================================
 * Open our socket, set the settings, and return the fd for the HTTP listener
 *==============================================================================
 */
static int bind_listener(char *ip, int port) {
	struct sockaddr_in	me;
	int					s = -1;
	int					sopt = 1;
	int					flags;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if(s < 0) {
		wlog(LOG_ERROR, "%s: socket() failed: %s", __func__, strerror(errno));
		goto err;
	}
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &sopt, sizeof(sopt)) < 0) {
		wlog(LOG_ERROR, "%s: setsockopt(SO_REUSEADDR) failed: %s", __func__, strerror(errno));
		goto err;
	}
	flags = fcntl(s, F_GETFL, 0);
	if(flags < 0) {
		wlog(LOG_ERROR, "%s: fcntl(F_GETFL) failed: %s", __func__, strerror(errno));
		goto err;
	}
	flags |= O_NONBLOCK;
	if(fcntl(s, F_SETFL, flags) < 0) {
		wlog(LOG_ERROR, "%s: fcntl(F_SETFL) failed: %s", __func__, strerror(errno));
		goto err;
	}
	
	memset(&me, 0, sizeof(struct sockaddr_in));
	if(inet_pton(AF_INET, ip, (void *)&me.sin_addr) != 1) {
		wlog(LOG_ERROR, "%s: inet_pton(%s) failed: %s", ip, __func__, strerror(errno));
		goto err;
	}
	me.sin_port = htons(port);
	me.sin_family = AF_INET;

	if(bind(s, (struct sockaddr *)&me, sizeof(struct sockaddr_in)) < 0) {
		wlog(LOG_ERROR, "%s: bind() failed: %s", __func__, strerror(errno));
		goto err;
	}

	if(listen(s, BACKLOG) < 0) {
		wlog(LOG_ERROR, "%s: listen(%d) failed: %s", __func__, BACKLOG, strerror(errno));
		goto err;
	}
	return s;

err:
	if(s >= 0) close(s);
	return -1;
}

/*------------------------------------------------------------------------------
 * Find a string within another one, but stopping before we overflow the number
 * of bytes available, returns the index of the match (or -1 if not found)
 *------------------------------------------------------------------------------
 */
int find_string(char *haystack, char *needle, int n) {
	int		i;
	int		l = strlen(needle);

	for(i = 0; i < (n-l); i++) {
		if(strncmp(haystack+i, needle, l) == 0) {
			return i;
		}
	}
	return -1;
}

/*------------------------------------------------------------------------------
 * Find the "Content-Length: <nnn>" from the given text, the text will be zero
 * terminated, so we can use normal stuff to find it.
 *------------------------------------------------------------------------------
 */
int find_content_length(char *haystack) {
	char *p = strstr(haystack, "Content-Length: ");
	if(!p) return -1;
	p += 16;
	return atoi(p);
}

/*------------------------------------------------------------------------------
 * New clients get added at the head of the list (for simplicity)
 *------------------------------------------------------------------------------
 */
struct client *new_client(int fd) {
	struct client *c = malloc(sizeof(struct client));
	if(!c) {
		wlog(LOG_FATAL, "%s: unable to create client: malloc(): %s", __func__, strerror(errno));
		exit(1);
	}
	memset(c, 0, sizeof(struct client));
	c->fd = fd;
	c->next = clients->next;
	clients->next = c;
	return c;
}

/*------------------------------------------------------------------------------
 * Here we read a chunk of data from the socket, we extend the buffer if we
 * need to
 *------------------------------------------------------------------------------
 */
int read_client_data(struct client *c) {
	int 	space = c->b_size - c->complete;
	int 	n;
	char 	*b;

	if(space < 2048) {
		c->buffer = realloc(c->buffer, c->b_size + 8192);
		if(!c->buffer) {
			wlog(LOG_ERROR, "%s: unable to extend buffer: realloc(): %s", __func__, strerror(errno));
			c->state = 9;				// discard client
			return -1;
		}
		c->b_size += 8192;
		space = c->b_size - c->complete;
	}
	b = c->buffer + c->complete;
	n = read(c->fd, b, space);
	if(n == 0) {
		fprintf(stderr, "EOF\n");
		c->state = 9;					// discard client
	} else if(n < 0) {
		wlog(LOG_ERROR, "%s: read error on client: %s", __func__, strerror(errno));
		c->state = 9;					// discard client
	} else {
		c->complete += n;
	}
	return n;
}

/*------------------------------------------------------------------------------
 * Local the end of HTTP header, and then the content length. If we have 2k
 * and still can't find it, we'll close
 *------------------------------------------------------------------------------
 */
int process_http_header(struct client *c) {
	int cl;
	int i = find_string(c->buffer, HTTP_SEP, c->complete);
	if(i >= 0) {
		*(c->buffer + i) = '\0';
		cl = find_content_length(c->buffer);
		if(cl <= 0) {
			c->state = 9;
			return -1;
		}
		i += 4;				// get past the text
		fprintf(stderr, "completed=%d i=%d cl=%d\n", c->complete, i, cl);
		c->complete -= i;
		memmove(c->buffer, c->buffer+i, c->complete);
		c->expected = cl;
		c->state = 1;
		return cl;
	} else if(c->complete >= 2048) {
		wlog(LOG_WARN, "%s: nothing found within 2048, discarding", __func__);
		c->state = 9;
		return -1;
	}
	return 0;
}

/*==============================================================================
 * This is the main select loop responsible for building and maintaining the
 * clients list
 *==============================================================================
 */
static int select_loop() {
	struct client		*c;
	fd_set				fd_r;
	fd_set				fd_w;
	int					max_fd = http_fd;
	int					rc = 0;

	FD_ZERO(&fd_r);
	FD_ZERO(&fd_w);

	FD_SET(http_fd, &fd_r);

	// Build fdsets...	
	for(c=clients->next; c; c=c->next) {
		if(c->state == 0 || c->state == 1) {
			// Handle read state...
			FD_SET(c->fd, &fd_r);
			if(c->fd > max_fd) max_fd = c->fd;
		} else 
		if(c->state == 4) {
			// Handle write state...		
	 		FD_SET(c->fd, &fd_w);
			if(c->fd > max_fd) max_fd = c->fd;
		}
	}

	// Now we can select...
	rc = select(max_fd+1, &fd_r, &fd_w, NULL, NULL);
	fprintf(stderr, "select rc=%d\n", rc);

	// See if any of our clients are ready to do stuff...
	for(c=clients->next; c; c=c->next) {
		if(FD_ISSET(c->fd, &fd_r)) {
			// We are ready to read...
			if(read_client_data(c) <= 0) continue;

			// At this point we have some data, if we are state 0 then
			// we need to look for the end of the header, we'll only do this up
			// to 2k of data, if we don't have it by then it's a problem
			if(c->state == 0) {
				if(process_http_header(c) < 1) continue;
			}
			
			// If we get here, then we are reading real content, so we
			// need to check if we have the right amount...
			if(c->state == 1) {
				fprintf(stderr, "now have %d of %d\n", c->complete, c->expected);
				if(c->complete >= c->expected) {
					c->state = 2;
				}
			}
		}
		if(FD_ISSET(c->fd, &fd_w)) {
			fprintf(stderr, "Write for client %p\n", c);
			// We are ready to write...
			int togo = c->expected - c->complete;
			int len = write(c->fd, c->buffer+c->complete, togo);
			fprintf(stderr, "write=%d togo=%d\n", len, togo);
			if(len < 0) {
				wlog(LOG_ERROR, "%s: write error on client: %s", __func__, strerror(errno));
				c->state = 9;
				continue;
			}
			c->complete += len;
			if(c->complete == c->expected) {
				// We are done, we can close
				c->state = 9;
				continue;
			}
		}
	}

	// Deal with the http fd, this means we are ready to accept
	// a new incoming connection, so need to create a new client
	if(FD_ISSET(http_fd, &fd_r)) {
		struct sockaddr_in	peer;
		socklen_t			len = sizeof(struct sockaddr_in);
		int					fd;

		fd = accept(http_fd, (struct sockaddr *)&peer, &len);
		if(fd < 0) {
			wlog(LOG_ERROR, "%s: accept() failed: %s", __func__, strerror(errno));
			return -1;
		}
		
		c = new_client(fd);
		// TODO: if peer is localhost, then we process differently
		c->state = 0;
	}
	return 1;
}

/*------------------------------------------------------------------------------
 * Find the first ready client (if there is one), also run through cleaning
 * up any clients that we are finished with.
 *------------------------------------------------------------------------------
 */
struct client *clean_and_find_ready() {
	struct client *c, *d, *rc = NULL;

	c = clients;
	while(c->next) {
		// Find the first one in state 2 to return...
		if(rc == NULL && c->next->state == 2) {
			rc = c->next;
			rc->state = 3;
		}
		// Remove any in state 9
		if(c->next->state == 9) {
			fprintf(stderr, "freeing client %p\n", c->next);
			d = c->next;
			if(d->buffer) free(d->buffer);
			close(d->fd);
			c->next = d->next;
			free(d);
			continue;
		}
		c = c->next;
	}
	return rc;
}
/*------------------------------------------------------------------------------
 * Simple routine to convert binary to hex, this uses a global so can't be used
 * multithreaded.
 *
 * Unhex requires a buffer of suitable length to be provided, so takes a max
 * value to limit the size of the output.
 *------------------------------------------------------------------------------
 */
#define MAX_TOHEX	32
static char hexbuf[(MAX_TOHEX * 2)+1];

char *hex(unsigned char *buf, int len) {
	int i;
	char *p = hexbuf;

	if(len > MAX_TOHEX) {
		wlog(LOG_FATAL, "%s: too long hex conversion (%d, MAX=%d)", __func__, len, MAX_TOHEX);
		exit(1);
	}
	for(i=0; i<len; i++) {
		sprintf(p, "%02x", buf[i]);
		p += 2;
	}
	*p = 0;
	return hexbuf;
}

int unhex(char *string, uint8_t *buf, int max) {
	static const char 	hl[16] = "0123456789abcdef";
	uint8_t				v;
	int					i = 0;

	if(strspn(string, hl) != strlen(string)) return 0;

	while(*string && *(string+1) && i++ < max) {
		v = (uint8_t)(strchr(hl, tolower(*string++)) - hl);
		*buf = (v << 4);
		v = (uint8_t)(strchr(hl, tolower(*string++)) - hl);
		*buf++ |= v;
	}
	return i;
}


/*------------------------------------------------------------------------------
 * Initialise the OpenSSL library ready for future encryption/decryption needs
 *------------------------------------------------------------------------------
 */
int init_ssl() {
	ERR_load_crypto_strings();
	OpenSSL_add_all_algorithms();
	OPENSSL_config(NULL);

	return 0;
}

char *decrypt(struct client *c, unsigned char *key, uint8_t *data, int len) {
	int 				tlen, olen;
	unsigned char		*plain = malloc(len+1);
	unsigned char		*iv = c->iv;
	unsigned long		err;
	EVP_CIPHER_CTX		*ctx;

	if(!plain) goto err;
	if(!(ctx = EVP_CIPHER_CTX_new())) {
		wlog(LOG_ERROR, "%s: EVP_CIPHER_CTX_new() failed", __func__);
		goto err;
	}
	if(EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv) != 1) {
		wlog(LOG_ERROR, "%s: EVP_DecryptInit_ex() failed", __func__);
		goto err;
	}
	if(EVP_DecryptUpdate(ctx, plain, &tlen, data, len) != 1) {
		wlog(LOG_ERROR, "%s: EVP_DecryptUpdate() failed", __func__);
		goto err;
	}
	olen = tlen;
	if(EVP_DecryptFinal(ctx, plain+tlen, &tlen) != 1) {
		wlog(LOG_ERROR, "%s: EVP_DecryptFinal() failed", __func__);
		goto err;
	}
	olen += tlen;
	plain[olen] = '\0';
	EVP_CIPHER_CTX_free(ctx);
	return (char *)plain;

err:
	while((err = ERR_get_error())) {
		wlog(LOG_ERROR, "%s: SSL Error: %s", __func__, ERR_error_string(err, NULL));
	}
	if(ctx) EVP_CIPHER_CTX_free(ctx);
	if(plain) free(plain);
	return NULL;
}

char *encrypt(struct client *c, unsigned char *key, char *plain, int *len) {
	int 				tlen, olen;
	unsigned char		*crypt = malloc(*len + 64);		// allow for block
	unsigned char		*iv = c->iv;
	unsigned long		err;
	EVP_CIPHER_CTX		*ctx;

	if(!crypt) goto err;
	if(!(ctx = EVP_CIPHER_CTX_new())) {
		wlog(LOG_ERROR, "%s: EVP_CIPHER_CTX_new() failed", __func__);
		goto err;
	}
	if(EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv) != 1) {
		wlog(LOG_ERROR, "%s: EVP_EncryptInit_ex() failed", __func__);
		goto err;
	}
	if(EVP_EncryptUpdate(ctx, crypt, &tlen, (unsigned char *)plain, *len) != 1) {
		wlog(LOG_ERROR, "%s: EVP_EncryptUpdate() failed", __func__);
		goto err;
	}
	olen = tlen;
	if(EVP_EncryptFinal(ctx, crypt+tlen, &tlen) != 1) {
		wlog(LOG_ERROR, "%s: EVP_EncryptFinal() failed", __func__);
		goto err;
	}
	olen += tlen;
	*len = olen;
	EVP_CIPHER_CTX_free(ctx);
	return (char *)crypt;

err:
	while((err = ERR_get_error())) {
		wlog(LOG_ERROR, "%s: SSL Error: %s", __func__, ERR_error_string(err, NULL));
	}
	if(ctx) EVP_CIPHER_CTX_free(ctx);
	if(crypt) free(crypt);
	return NULL;
}

/*==============================================================================
 * Provide our service data for the unit_service module
 *
 * decrypt(table, key)
 * encrypt(table, key)
 *==============================================================================
 */
static int do_decrypt(lua_State *L) {
	struct client 	*c;
	char			*data, *p;
	uint8_t			key[17];
	char			*err;
//	uint8_t 		key[17] = {
//			0xba, 0x86, 0xf2, 0xbb, 0xe1, 0x07, 0xc7, 0xc5,
//			0x7e, 0xb5, 0xf2, 0x69, 0x07, 0x75, 0xc7, 0x12, 0x00 };

	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TSTRING);

	// Get the client reference...
	lua_getfield(L, 1, "_ref");
	if(!lua_islightuserdata(L, -1)) {
		err = "invalid table, no client reference";
		goto err1;
	}
	c = (struct client *)lua_topointer(L, -1);
	lua_pop(L, 1);

	// Build the key...
	p = (char *)lua_tostring(L, 2);
	if(strlen(p) != 32 || unhex(p, key, 16) != 16) {
		err = "invalid key - expect 16 hex digits";
		goto err;
	}

	// Decrypt the data
	data = decrypt(c, key, c->data, c->data_length);
	if(!data) {
		err = "unable to decrypt";
		goto err;
	}

	// Now we can unserialise it... keep a ref so we can free it
	p = data;
	lua_pushstring(L, "data");
	if(!unserialise_variable(L, &p)) {
		err = "unable to decode data";
		goto err1;
	}
	lua_settable(L, 1);
	free(data);

	lua_pushboolean(L, 1);	
	return 1;

err1:	lua_pop(L, 1);
err:	lua_pushboolean(L, 0);
		lua_pushstring(L, err);
		return 2;
}

static int do_encrypt(lua_State *L) {
	struct client 	*c;
	char			*data, *p;
	uint8_t			key[17];
	char			*err;
	int				plen;

	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TSTRING);

	// Get the client reference...
	lua_getfield(L, 1, "_ref");
	if(!lua_islightuserdata(L, -1)) {
		err = "invalid table, no client reference";
		goto err1;
	}
	c = (struct client *)lua_topointer(L, -1);
	lua_pop(L, 1);

	// Build the key...
	p = (char *)lua_tostring(L, 2);
	if(strlen(p) != 32 || unhex(p, key, 16) != 16) {
		err = "invalid key - expect 16 hex digits";
		goto err;
	}

	// Serialise the data
	lua_pushcfunction(L, serialise);
	lua_getfield(L, 1, "data");			// arg to serialise
	lua_call(L, 1, 1);
	if(!lua_isstring(L, -1)) {
		fprintf(stderr, "AAARGGG\n");
		goto err1;
	}
	p = (char *)lua_tostring(L, -1);
	plen = strlen(p);

	// Encrypt the data
	data = encrypt(c, key, p, &plen);
	lua_pop(L, 1);						// remove serialised string
	if(!data) {
		err = "unable to decrypt";
		goto err;
	}

	// Now put the encrypted data into the table
	lua_pushstring(L, "data");
	lua_pushlstring(L, data, plen);
	lua_settable(L, 1);
	free(data);

	lua_pushboolean(L, 1);	
	return 1;

err1:	lua_pop(L, 1);
err:	lua_pushboolean(L, 0);
		lua_pushstring(L, err);
		return 2;
}

/*==============================================================================
 * Provide our service data for the unit_service module
 *==============================================================================
 */
static int init(lua_State *L) {

	struct client *head = malloc(sizeof(struct client));
	if(!head) {
		wlog(LOG_FATAL, "%s: unable to create client head: malloc(): %s", __func__, strerror(errno));
		return 0;
	}
	head->next = NULL;
	clients = head;

	int rc = bind_listener("127.0.0.1", 2345);
	fprintf(stderr, "rc=%d\n", rc);

	http_fd = rc;

	// Initialise SSL
	init_ssl();

	lua_pushnumber(L, 0);
	return 1;
}

#define TABLE(f, t, v...) 	lua_pushstring(L, f); \
							lua_push##t(L, v); \
							lua_settable(L, -3)

static int get_client(lua_State *L) {
	struct client 	*c;
	char			mac[18];

	do {
		c = clean_and_find_ready();
		if(c) break;

		select_loop();
	} while(1);

	// TODO: here we need to create a table populated with the main
	//       header fields and he data
	
	struct inform	*hdr = (struct inform *)c->buffer;
	int				flags = ntohs(hdr->flags);

	// Keep some useful fields handy...
	c->data_length = ntohl(hdr->data_length);
	c->data = (uint8_t *)(hdr+1);
	memcpy(c->iv, hdr->iv, 16);
	memcpy(c->mac, hdr->mac, 6);

	// Put the mac address into a standard format...
	sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", hdr->mac[0], hdr->mac[1],
					hdr->mac[2], hdr->mac[3], hdr->mac[4], hdr->mac[5]);

	lua_newtable(L);
	
	TABLE("magic", lstring, hdr->magic, 4);
	TABLE("version", number, ntohl(hdr->version));
	TABLE("mac", string, mac);
	TABLE("encrypted", boolean, (flags&1) ? 1 : 0);
	TABLE("compressed", boolean, (flags&2) ? 1 : 0);
	TABLE("iv", string, hex(hdr->iv, 16));
	TABLE("dataversion", number, ntohl(hdr->data_version));
	TABLE("datalength", number, c->data_length);
	TABLE("inform", boolean, 1);
	TABLE("_ref", lightuserdata, (void *)c);

	// TODO: deal with compression

	// We only unserialise the data here if it's not encrypted, if we
	// can't unserialise then we'll clear the data field (nil)
	if((flags&1) == 0) {
		char *data = (char *)(hdr+1);

		lua_pushstring(L, "data");
		if(!unserialise_variable(L, &data)) lua_pushnil(L);
		lua_settable(L, -3);
	}
	return 1;
}

/*==============================================================================
 * We take a client table (including _ref) and form an http reply which we send
 * back on the socket (by scheduling for the select_loop)
 *==============================================================================
 */
static void add_reply_string(struct client *c, char *str, int l) {
	int space = c->b_size - c->expected;

	if(l==0) l = strlen(str);
	if(space < l) {
		c->buffer = realloc(c->buffer, c->b_size + l + 2048);
		c->b_size += l + 2048;
	}
	memcpy(c->buffer + c->expected, str, l);
	c->expected += l;
}

static char http_400[] = "Bad Request";
static char http_200[] = "OK";
static char http_UNK[] = "Unknown";

static char *http_code(int status) {
	switch(status) {
	case 200:	return http_200;
	case 400:	return http_400;
	default:	return http_UNK;
	}
}

static int do_reply(lua_State *L) {
	struct client	*c;
	time_t			t;
	struct tm		*tmp;
	char			line[256];
	char			date[128];
	char			*data = NULL;
	size_t			len, content_len;
	int				encrypted = 0;

	luaL_checktype(L, 1, LUA_TTABLE);

	// Get the client reference...
	lua_getfield(L, 1, "_ref");
	if(!lua_islightuserdata(L, -1)) {
//		err = "invalid table, no client reference";
		goto err1;
	}
	c = (struct client *)lua_topointer(L, -1);
	lua_pop(L, 1);

	// Pull out specific values from the table into the header
	lua_getfield(L, 1, "status");
	c->status = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, 1, "encrypted");
	if(lua_isboolean(L, -1)) encrypted = lua_toboolean(L, -1);
	lua_pop(L, 1);

	// We will be sending "data", if it's null then we are zero length,
	// if it's a string then we send it (probably encrypted), if it's
	// anything else we serialise.
	lua_getfield(L, 1, "data");
	if(lua_isnil(L, -1)) {
		len = 0;
	} else {
		// If it's not a string then we serialise
		if(!lua_isstring(L, -1)) {
			lua_pop(L, 1);			// remove the field
			lua_pushcfunction(L, serialise);
			lua_getfield(L, 1, "data");			// arg to serialise
			lua_call(L, 1, 1);
		}
		// Now it really should be a string...
		if(!lua_isstring(L, -1)) {
			fprintf(stderr, "AAARGGG\n");
			goto err1;
		}
		data = (char *)lua_tolstring(L, -1, &len);
	}
	// At this point we have one extra item on the stack to remove later

	// Now clear our data so we start building the buffer
	// from scratch (b_size is still correct though)
	c->expected = 0;
	
	// Now formulate a payload, start with preparing the date...
	t = time(NULL);
	tmp = localtime(&t);
	strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", tmp);

	// Now the initial response...
	content_len = (len ? len + sizeof(struct inform) : 0);
	snprintf(line, sizeof(line), "HTTP/1.1 %03d %s\nContent-Length: %d\n",
						c->status, http_code(c->status), (int)content_len);
	add_reply_string(c, line, 0);
	
	// Secondary data...
	snprintf(line, sizeof(line), "Date: %s\nConnection: close", date);
	add_reply_string(c, line, 0);
	
	// Close the main header
	add_reply_string(c, HTTP_SEP, 0);

	// Now the data ... if we have it ...
	if(len) {
		// First we need a header
		struct inform	hdr;

		memcpy(hdr.magic, INFORM_MAGIC, 4);
		hdr.version = htonl(0);
		memcpy(hdr.mac, c->mac, 6);
		hdr.flags = htons(encrypted);				// TODO: compressed
		memcpy(hdr.iv, c->iv, 16);
		hdr.data_version = htonl(1);
		hdr.data_length = htonl(len);
	
		add_reply_string(c, (char *)&hdr, sizeof(struct inform));
		add_reply_string(c, data, len);
	}

	// Remove our data item (or nil) from the stack
	lua_pop(L, 1);

	// Now mark as ready to send...
	c->complete = 0;
	c->state = 4;

	lua_pushboolean(L, 1);
	return 1;	

err1: lua_pop(L, 1);
	  lua_pushboolean(L, 0);
	  return 1;
}

/*==============================================================================
 * Given a table (data) we will serialise (in pretty format) and then write
 * it out to the named file.
 *
 * write_table(table, filename)
 *==============================================================================
 */
int write_table(lua_State *L) {
	char	*p;
	char	*fname;
	int		fd, plen;
	int 	rc = 0;

	luaL_checktype(L, 1, LUA_TSTRING);
	luaL_checktype(L, 2, LUA_TTABLE);

	// We'll call our lua "serialise" c function, so push the arg then we
	// should end up with a string.
	lua_pushcfunction(L, serialise);
	lua_pushvalue(L, 2);
	lua_call(L, 1, 1);
	if(!lua_isstring(L, -1)) {
		fprintf(stderr, "AAARGGG\n");
		goto err1;
	}
	p = (char *)lua_tostring(L, -1);
	fprintf(stderr, "serialised: %s\n", p);
	fname = (char *)lua_tostring(L, 1);
	plen = strlen(p);

	if((fd = open(fname, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) == -1) {
		wlog(LOG_ERROR, "%s: unable to create data file(%s): %s", __func__, fname, strerror(errno));
		goto err1;
	}
	if(write(fd, p, plen) != plen) {
		wlog(LOG_ERROR, "%s: unable to write all data(%s)", __func__);
		close(fd);
	}
	close(fd);
	rc = 1;		// all ok, return true

err1:	lua_pop(L, 1);		// remove the returned string
		lua_pushboolean(L, rc);
		return 1;
}

/*==============================================================================
 * Read a given filename, unserialise the data, and return the resulting value
 * (which is probably a table)
 *
 * read_table(filename)
 *==============================================================================
 */
int read_table(lua_State *L) {
	int		fd;
	char	*d = NULL, *fname;
	off_t	len;

	luaL_checktype(L, 1, LUA_TSTRING);
	fname = (char *)lua_tostring(L, 1);

	if((fd = open(fname, O_RDONLY)) == -1) {
		fprintf(stderr, "open(): err=%s\n", strerror(errno));
		goto err;
	}
	// Work out how long the file is...
	len = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	// Allocate a buffer and read...
	d = malloc(len+1);
	if(!d) {
		fprintf(stderr, "malloc problem\n");
		goto err;
	}
	fprintf(stderr, "len=%d\n", (int)len);
	if(read(fd, d, len) != len) {
		fprintf(stderr, "read problem\n");
		goto err;
	}
	fprintf(stderr, "about to serialise\n");
	fprintf(stderr, "%s", d);
	if(unserialise_variable(L, &d) != 1) goto err;
	return 1;

err:	if(d) free(d);
		if(fd >= 0) close(fd);
		return 0;
}

/*==============================================================================
 * These are the functions we export to Lua...
 *==============================================================================
 */
static const struct luaL_reg lib[] = {
	{"init", init},
	{"get_client", get_client},
	{"reply", do_reply},
	{"decrypt", do_decrypt},
	{"encrypt", do_encrypt},
	{"write_table", write_table},
	{"read_table", read_table},
	{"unserialise", unserialise},		// see serialise.c
	{"serialise", serialise},			// see serialise.c
	{"time", get_time},					// see utils.c
	{"add_cfg", add_cfg},				// see utils.c
	{"gen_hex", gen_hex},				// see utils.c
//	{"log", lua_log},		// TODO
	{NULL, NULL}
};

/*------------------------------------------------------------------------------
 * Main Library Entry Point ... just intialise all the functions
 *------------------------------------------------------------------------------
 */
int luaopen_unisvr(lua_State *L) {
	// Initialise the module...
	luaL_openlib(L, "unisvr", lib, 0);

	if(!(logf = fopen(LOG_FILE, "a"))) {
		fprintf(stderr, "WARNING: unable to open log file (%s): %s\n", LOG_FILE, strerror(errno));
	}
	wlog(LOG_WARN, "starting.");

	return 1;
}

