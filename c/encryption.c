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
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include <sys/types.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include "log.h"

/*------------------------------------------------------------------------------
 * Initialise the OpenSSL library ready for future encryption/decryption needs
 *------------------------------------------------------------------------------
 */
int init_encryption() {
	ERR_load_crypto_strings();
	OpenSSL_add_all_algorithms();
	OPENSSL_config(NULL);

	return 0;
}

char *decrypt(uint8_t *key, uint8_t *iv, uint8_t *data, int len) {
	int 				tlen, olen;
	unsigned char		*plain = malloc(len+1);
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

char *encrypt(uint8_t *key, uint8_t *iv, char *plain, int *len) {
	int 				tlen, olen;
	unsigned char		*crypt = malloc(*len + 64);		// allow for block
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

