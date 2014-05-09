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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

/*==============================================================================
 * Globals (local to this module)
 *==============================================================================
 */
#define BACKLOG			128				// max outstanding connections

static char				HTTP_SEP[5] = { 0x0d, 0x0a, 0x0d, 0x0a, 0x00 };
static int				http_fd;		// socket for http listener

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
	uint8_t			data[1];
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
	uint32_t		data_length;
	uint8_t			*data;
	uint8_t			iv[16];
};

struct client 		*clients;

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
		fprintf(stderr, "socket failed\n");
		goto err;
	}
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &sopt, sizeof(sopt)) < 0) {
		fprintf(stderr, "setsockopt failed\n");
		goto err;
	}
	flags = fcntl(s, F_GETFL, 0);
	if(flags < 0) {
		fprintf(stderr, "unable to fcntl GETFL\n");
		goto err;
	}
	flags |= O_NONBLOCK;
	if(fcntl(s, F_SETFL, flags) < 0) {
		fprintf(stderr, "unable to fcntl SETFL\n");
		goto err;
	}
	
	memset(&me, 0, sizeof(struct sockaddr_in));
	if(inet_pton(AF_INET, ip, (void *)&me.sin_addr) != 1) {
		fprintf(stderr, "inet_pton failed\n");
		goto err;
	}
	me.sin_port = htons(port);
	me.sin_family = AF_INET;

	if(bind(s, (struct sockaddr *)&me, sizeof(struct sockaddr_in)) < 0) {
		fprintf(stderr, "bind failed\n");
		goto err;
	}

	if(listen(s, BACKLOG) < 0) {
		fprintf(stderr, "listen failed\n");
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
		fprintf(stderr, "fatal: unable to create client\n");
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
int read_data(struct client *c) {
	int 	space = c->b_size - c->complete;
	int 	n;
	char 	*b;

	if(space < 2048) {
		c->buffer = realloc(c->buffer, c->b_size + 8192);
		if(!c->buffer) {
			fprintf(stderr, "fatal: unable to extend buffer\n");
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
		fprintf(stderr, "READ ERROR\n");
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
		fprintf(stderr, "content length=%d\n", cl);
		if(cl <= 0) {
			c->state = 9;
			return -1;
		}
		i += 4;				// get past the text
		c->complete -= i;
		memmove(c->buffer, c->buffer+i, c->complete);
		c->expected = cl;
		c->state = 1;
		return cl;
	} else if(c->complete >= 2048) {
		fprintf(stderr, "nothing found within 2048, discarding\n");
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
			if(read_data(c) <= 0) continue;

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
		} else if(FD_ISSET(c->fd, &fd_w)) {
			// We are ready to write...
			int togo = c->expected - c->complete;
			int len = write(c->fd, c->buffer+c->complete, togo);
			if(len < 0) {
				fprintf(stderr, "write error\n");
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
			fprintf(stderr, "accept failed\n");
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
 *------------------------------------------------------------------------------
 */
#define MAX_TOHEX	32
static char hexbuf[(MAX_TOHEX * 2)+1];

char *hex(unsigned char *buf, int len) {
	int i;
	char *p = hexbuf;

	if(len > MAX_TOHEX) {
		fprintf(stderr, "FATAL: to long hex conversion\n");
		exit(1);
	}
	for(i=0; i<len; i++) {
		sprintf(p, "%02x", buf[i]);
		p += 2;
	}
	*p = 0;
	return hexbuf;
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

char *decrypt(struct client *c, unsigned char *key) {
	int 				ilen = c->data_length;
	int 				tlen, olen;
	unsigned char		*plain = malloc(ilen+1);
	unsigned char		*iv = c->iv;
	EVP_CIPHER_CTX		*ctx;

	if(!plain) goto err;
	if(!(ctx = EVP_CIPHER_CTX_new())) {
		fprintf(stderr, "unable to create cipher context\n");
		goto err;
	}
	if(EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv) != 1) {
		fprintf(stderr, "unable to initialise decrypt\n");
		goto err;
	}
	if(EVP_DecryptUpdate(ctx, plain, &tlen, c->data, ilen) != 1) {
		fprintf(stderr, "unable to decrypt\n");
		goto err;
	}
	fprintf(stderr, "tlen=%d ilen=%d\n", tlen, ilen);
	olen = tlen;
	if(EVP_DecryptFinal(ctx, plain+tlen, &tlen) != 1) {
		fprintf(stderr, "unable to finish decrypt\n");
		goto err;
	}
	olen += tlen;
	fprintf(stderr, "tlen=%d ilen=%d\n", tlen, ilen);
	plain[olen] = '\0';
	EVP_CIPHER_CTX_free(ctx);
	return (char *)plain;

err:
	ERR_print_errors_fp(stderr);
	if(ctx) EVP_CIPHER_CTX_free(ctx);
	if(plain) free(plain);
	return NULL;
}
/*==============================================================================
 * Provide our service data for the unit_service module
 *==============================================================================
 */
static int do_decrypt(lua_State *L) {
	struct client 	*c;
	char			*data;
	uint8_t 		key[17] = {
			0xba, 0x86, 0xf2, 0xbb, 0xe1, 0x07, 0xc7, 0xc5,
			0x7e, 0xb5, 0xf2, 0x69, 0x07, 0x75, 0xc7, 0x12, 0x00 };

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "_ref");
	c = (struct client *)lua_topointer(L, -1);
	fprintf(stderr, "c=0x%p\n", c);

	data = decrypt(c, key);
	fprintf(stderr, "DATA: %s\n", data);
	
	return 0;
}

/*==============================================================================
 * Provide our service data for the unit_service module
 *==============================================================================
 */
static int init(lua_State *L) {

	struct client *head = malloc(sizeof(struct client));
	if(!head) {
		fprintf(stderr, "unable to malloc client head\n");
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
	struct client *c;

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
	c->data = hdr->data;
	memcpy(c->iv, hdr->iv, 16);

	lua_newtable(L);
	
	TABLE("magic", lstring, hdr->magic, 4);
	TABLE("version", number, ntohl(hdr->version));
	TABLE("mac", string, hex(hdr->mac, 6));
	TABLE("encrypted", number, (flags&1) ? 1 : 0);
	TABLE("compressed", number, (flags&2) ? 1 : 0);
	TABLE("iv", string, hex(hdr->iv, 16));
	TABLE("dataversion", number, ntohl(hdr->data_version));
	TABLE("datalength", number, c->data_length);
	TABLE("_ref", lightuserdata, (void *)c);

	// TODO: deal with compression

	// We only put the data here if it's not encrypted
	if((flags&1) == 0) {
		TABLE("data", lstring, (char *)hdr->data, ntohl(hdr->data_length));
	}
	fprintf(stderr, "c=0x%p\n", c);
	return 1;
}

/*==============================================================================
 * These are the functions we export to Lua...
 *==============================================================================
 */
static const struct luaL_reg lib[] = {
	{"init", init},
	{"get_client", get_client},
	{"decrypt", do_decrypt},
	{NULL, NULL}
};

/*------------------------------------------------------------------------------
 * Main Library Entry Point ... just intialise all the functions
 *------------------------------------------------------------------------------
 */
int luaopen_unisvr(lua_State *L) {
	// Initialise the module...
	luaL_openlib(L, "unisvr", lib, 0);

	return 1;
}

