/*
 *	SSLv3/TLSv1 client-side functions
 *
 *	Based on XySSL: Copyright (C) 2006-2008	 Christophe Devine
 *
 *	Copyright (C) 2009	Paul Bakker <polarssl_maintainer at polarssl dot org>
 *
 *	All rights reserved.
 *
 *	Redistribution and use in source and binary forms, with or without
 *	modification, are permitted provided that the following conditions
 *	are met:
 *
 *	  * Redistributions of source code must retain the above copyright
 *		notice, this list of conditions and the following disclaimer.
 *	  * Redistributions in binary form must reproduce the above copyright
 *		notice, this list of conditions and the following disclaimer in the
 *		documentation and/or other materials provided with the distribution.
 *	  * Neither the names of PolarSSL or XySSL nor the names of its contributors
 *		may be used to endorse or promote products derived from this software
 *		without specific prior written permission.
 *
 *	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *	FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *	OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *	TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *	LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *	NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tropicssl/config.h"

#if defined(TROPICSSL_SSL_CLI_C)

#include "tropicssl/debug.h"
#include "tropicssl/ssl.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static int ssl_write_client_hello(ssl_context * ssl)
{
	int ret, i, n, k;
	unsigned char *buf;
	unsigned char *p;
	time_t t;

	SSL_DEBUG_MSG(2, ("=> write client hello"));

	if(ssl->datagram) {
		ssl->major_ver = DTLS_MAJOR_VERSION_1;
		ssl->minor_ver = DTLS_MINOR_VERSION_0;

		ssl->max_major_ver = DTLS_MAJOR_VERSION_1;
		ssl->max_minor_ver = DTLS_MINOR_VERSION_0;
	} else {
		ssl->major_ver = SSL_MAJOR_VERSION_3;
		ssl->minor_ver = SSL_MINOR_VERSION_0;

		ssl->max_major_ver = SSL_MAJOR_VERSION_3;
		ssl->max_minor_ver = SSL_MINOR_VERSION_1;
	}

	/*
	 *         0  .   0       handshake type
	 *         1  .   3       handshake length
	 *         4  .   5       highest version supported
	 *         6  .   9       current UNIX time
	 *        10  .  37       random bytes
	 */
	buf = ssl->out_msg;
	p = buf + 4;
	if(ssl->datagram) {
		/*
		*         4  .   5       message sequence
		*         6  .   8       fragment offet
		*		  9  .  11       fragment length
		*/
		p += 8;
	}

	*p++ = (unsigned char)ssl->max_major_ver;
	*p++ = (unsigned char)ssl->max_minor_ver;

	if(ssl->datagram) {
		SSL_DEBUG_MSG(3, ("client hello, max version: [%d:%d]",
			buf[8+4], buf[8+5]));
	} else {
		SSL_DEBUG_MSG(3, ("client hello, max version: [%d:%d]",
			buf[4], buf[5]));
	}
	t = time(NULL);
	*p++ = (unsigned char)(t >> 24);
	*p++ = (unsigned char)(t >> 16);
	*p++ = (unsigned char)(t >> 8);
	*p++ = (unsigned char)(t);

	SSL_DEBUG_MSG(3, ("client hello, current time: %lu", t));

	for (i = 28; i > 0; i--)
		*p++ = (unsigned char)ssl->f_rng(ssl->p_rng);

	memcpy(ssl->randbytes, buf + (ssl->datagram ? 6+8 : 6), 32);

	SSL_DEBUG_BUF(3, "client hello, random bytes", buf + (ssl->datagram ? 6+8 : 6), 32);

	/*
	 *        38  .  38       session id length
	 *        39  . 39+n  session id
	 *     40+8+n . 40+8+n  cookie length
	 *     41+8+n . 41+8+k  cookie
	 *       41+k . 42+k  cipherlist length
	 *       43+k . ..        cipherlist
	 *       ..       . ..    compression alg. (0)
	 *       ..       . ..    extensions (unused)
	 */
	n = ssl->session->length;

	if (n < 16 || n > 32 || ssl->resume == 0 ||
	    t - ssl->session->start > ssl->timeout)
		n = 0;

	*p++ = (unsigned char)n;

	for (i = 0; i < n; i++)
		*p++ = ssl->session->id[i];

	SSL_DEBUG_MSG(3, ("client hello, session id len.: %d", n));
	SSL_DEBUG_BUF(3, "client hello, session id", buf + (ssl->datagram ? 39+8 : 39), n);

	// cookie
	if(ssl->datagram) {
		*p++ = (unsigned char)ssl->cookielen;
		for(k = 0; k < ssl->cookielen; k++)
			*p++ = (unsigned char)ssl->cookie[k];

		SSL_DEBUG_BUF(3, "client hello, cookie bytes", buf + 39+8+1+k, ssl->cookielen);
	}

	for (n = 0; ssl->ciphers[n] != 0; n++) ;
	*p++ = (unsigned char)(n >> 7);
	*p++ = (unsigned char)(n << 1);

	SSL_DEBUG_MSG(3, ("client hello, got %d ciphers", n));

	for (i = 0; i < n; i++) {
		SSL_DEBUG_MSG(3, ("client hello, add cipher: %2d",
				  ssl->ciphers[i]));

		*p++ = (unsigned char)(ssl->ciphers[i] >> 8);
		*p++ = (unsigned char)(ssl->ciphers[i]);
	}

	SSL_DEBUG_MSG(3, ("client hello, compress len.: %d", 1));
	SSL_DEBUG_MSG(3, ("client hello, compress alg.: %d", 0));

	*p++ = 1;
	*p++ = SSL_COMPRESS_NULL;

	if (ssl->hostname != NULL) {
		SSL_DEBUG_MSG(3, ("client hello, server name extension: %s",
				  ssl->hostname));

		*p++ = (unsigned char)(((ssl->hostname_len + 9) >> 8) & 0xFF);
		*p++ = (unsigned char)(((ssl->hostname_len + 9)) & 0xFF);

		*p++ = (unsigned char)((TLS_EXT_SERVERNAME >> 8) & 0xFF);
		*p++ = (unsigned char)((TLS_EXT_SERVERNAME) & 0xFF);

		*p++ = (unsigned char)(((ssl->hostname_len + 5) >> 8) & 0xFF);
		*p++ = (unsigned char)(((ssl->hostname_len + 5)) & 0xFF);

		*p++ = (unsigned char)(((ssl->hostname_len + 3) >> 8) & 0xFF);
		*p++ = (unsigned char)(((ssl->hostname_len + 3)) & 0xFF);

		*p++ = (unsigned char)((TLS_EXT_SERVERNAME_HOSTNAME) & 0xFF);
		*p++ = (unsigned char)((ssl->hostname_len >> 8) & 0xFF);
		*p++ = (unsigned char)((ssl->hostname_len) & 0xFF);

		memcpy(p, ssl->hostname, ssl->hostname_len);

		p += ssl->hostname_len;
	}

	ssl->out_msglen = p - buf;
	ssl->out_msgtype = SSL_MSG_HANDSHAKE;
	ssl->out_msg[0] = SSL_HS_CLIENT_HELLO;

	// set next sate
	if(ssl->datagram && ssl->cookielen == 0) {
		ssl->state = SSL_SERVER_HELLOVERIFY;
	} else {
		ssl->state = SSL_SERVER_HELLO;
	}

	if ((ret = ssl_write_record(ssl)) != 0) {
		SSL_DEBUG_RET(1, "ssl_write_record", ret);
		return (ret);
	}

	SSL_DEBUG_MSG(2, ("<= write client hello"));

	return (0);
}

static int ssl_parse_server_helloverify(ssl_context * ssl)
{
	unsigned char *buf;
	int ret;

	SSL_DEBUG_MSG(2, ("=> parse server hello verify"));

	/*
	 *         0  .   0       handshake type
	 *         1  .   3       handshake length
	 *		   4  .	  5		  message sequence
	 *		   6  .   8		  fragment offset
	 *		   9  .  12	  	  fragment length
	 *        12  .  13       protocol version
	 *        14  .  34       cookie
	 */
	buf = ssl->in_msg;

	if ((ret = ssl_read_handshake(ssl)) != 0) {
		SSL_DEBUG_RET(1, "ssl_read_handshake", ret);
		return (ret);
	}

	if (ssl->in_msgtype != SSL_MSG_HANDSHAKE) {
		SSL_DEBUG_MSG(1, ("bad server hello verify message"));
		return (TROPICSSL_ERR_SSL_UNEXPECTED_MESSAGE);
	}

	SSL_DEBUG_MSG(3, ("server hello verify, chosen version: [%d:%d]",
			  buf[12], buf[13]));

	if (ssl->in_hslen < 20 ||
	    buf[0] != SSL_HS_HELLO_VERIFY_REQUEST || buf[12] != DTLS_MAJOR_VERSION_1) {
		SSL_DEBUG_MSG(1, ("bad server hello verify message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_HELLO_VERIFY);
	}

	if (buf[13] != DTLS_MINOR_VERSION_0 && buf[13] != DTLS_MINOR_VERSION_2) {
		SSL_DEBUG_MSG(1, ("bad server hello verify message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_HELLO_VERIFY);
	}

	ssl->cookielen = buf[14];

	memcpy(ssl->cookie, buf + 15, ssl->cookielen);

	SSL_DEBUG_BUF(3, "server hello verify, cookie bytes", buf + 15, buf[14]);

	ssl->state = SSL_CLIENT_HELLO;

	// reset handshake length, so that next ssl_read_handshake does not fail
	ssl->in_hslen = 0;

	SSL_DEBUG_MSG(2, ("<= parser server hello verify"));

	return 0;
}

static int ssl_parse_server_hello(ssl_context * ssl)
{
	time_t t;
	int ret, i, n, doff;
	int ext_len;
	unsigned char *buf;

	SSL_DEBUG_MSG(2, ("=> parse server hello"));

	/*
	 *         0  .   0       handshake type
	 *         1  .   3       handshake length
	 *         4  .   5       protocol version
	 *         6  .   9       UNIX time()
	 *        10  .  37       random bytes
	 */
	buf = ssl->in_msg;

	if ((ret = ssl_read_handshake(ssl)) != 0) {
		SSL_DEBUG_RET(1, "ssl_read_handshake", ret);
		return (ret);
	}

	if (ssl->in_msgtype != SSL_MSG_HANDSHAKE) {
		SSL_DEBUG_MSG(1, ("bad server hello message"));
		return (TROPICSSL_ERR_SSL_UNEXPECTED_MESSAGE);
	}

	doff = ssl->datagram ? 8 : 0;

	SSL_DEBUG_MSG(3, ("server hello, chosen version: [%d:%d]",
					  buf[4 + doff], buf[5 + doff]));

	if (ssl->in_hslen < 42 ||
	    buf[0] != SSL_HS_SERVER_HELLO ||
		(ssl->datagram ? buf[4 + doff] != DTLS_MAJOR_VERSION_1
		: buf[4] != SSL_MAJOR_VERSION_3)) {
		SSL_DEBUG_MSG(1, ("bad server hello message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_HELLO);
	}

	if (buf[5 + doff] != SSL_MINOR_VERSION_0 &&
		(ssl->datagram
		 ?(buf[5 + doff] != DTLS_MINOR_VERSION_0 &&
		   buf[5 + doff] != DTLS_MINOR_VERSION_2)
		 : buf[5] != SSL_MINOR_VERSION_1)) {
		SSL_DEBUG_MSG(1, ("bad server hello message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_HELLO);
	}

	ssl->minor_ver = buf[5 + doff];

	t = ((time_t) buf[6 + doff] << 24)
	    | ((time_t) buf[7 + doff] << 16)
	    | ((time_t) buf[8 + doff] << 8)
	    | ((time_t) buf[9 + doff]);

	memcpy(ssl->randbytes + 32, buf + 6 + doff, 32);

	// session length
	n = buf[38 + doff];

	SSL_DEBUG_MSG(3, ("server hello, current time: %lu", t));
	SSL_DEBUG_BUF(3, "server hello, random bytes", buf + 6 + doff, 32);

	/*
	 *        38  .  38       session id length
	 *        39  . 38+n  session id
	 *       39+n . 40+n  chosen cipher
	 *       41+n . 41+n  chosen compression alg.
	 *       42+n . 43+n  extensions length
	 *       44+n . 44+n+m extensions
	 */
	if (n < 0 || n > 32 || ssl->in_hslen > 42 + n + doff) {
		ext_len = ((buf[42 + n + doff] << 8)
			   | (buf[43 + n + doff])) + 2;
	} else {
		ext_len = 0;
	}

	if (n < 0 || n > 32 || ssl->in_hslen != (42 + n + doff + ext_len)) {
		SSL_DEBUG_MSG(1, ("bad server hello message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_HELLO);
	}

	i = (buf[39 + n + doff] << 8) | buf[40 + n + doff];

	SSL_DEBUG_MSG(3, ("server hello, session id len.: %d", n));
	SSL_DEBUG_BUF(3, "server hello, session id", buf + 39 + doff, n);

	/*
	 * Check if the session can be resumed
	 */
	if (ssl->resume == 0 || n == 0 ||
	    ssl->session->cipher != i ||
	    ssl->session->length != n ||
	    memcmp(ssl->session->id, buf + 39 + doff, n) != 0) {
		ssl->state++;
		ssl->resume = 0;
		ssl->session->start = time(NULL);
		ssl->session->cipher = i;
		ssl->session->length = n;
		memcpy(ssl->session->id, buf + 39 + doff, n);
	} else {
		ssl->state = SSL_SERVER_CHANGE_CIPHER_SPEC;
		ssl_derive_keys(ssl);
	}

	SSL_DEBUG_MSG(3, ("%s session has been resumed",
			  ssl->resume ? "a" : "no"));

	SSL_DEBUG_MSG(3, ("server hello, chosen cipher: %d", i));
	SSL_DEBUG_MSG(3, ("server hello, compress alg.: %d", buf[41 + n + doff]));

	i = 0;
	while (1) {
		if (ssl->ciphers[i] == 0) {
			SSL_DEBUG_MSG(1, ("bad server hello message"));
			return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_HELLO);
		}

		if (ssl->ciphers[i++] == ssl->session->cipher)
			break;
	}

	if (buf[41 + n + doff] != SSL_COMPRESS_NULL) {
		SSL_DEBUG_MSG(1, ("bad server hello message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_HELLO);
	}

	/* TODO: Process extensions */

	SSL_DEBUG_MSG(2, ("<= parse server hello"));

	return (0);
}

static int ssl_parse_server_key_exchange(ssl_context * ssl)
{
	int ret, n, doff;
	unsigned char *p, *end;
	unsigned char hash[36];
	md5_context md5;
	sha1_context sha1;

	SSL_DEBUG_MSG(2, ("=> parse server key exchange"));

	if (ssl->session->cipher != SSL_EDH_RSA_DES_168_SHA &&
	    ssl->session->cipher != SSL_EDH_RSA_AES_256_SHA &&
	    ssl->session->cipher != SSL_EDH_RSA_CAMELLIA_256_SHA) {
		SSL_DEBUG_MSG(2, ("<= skip parse server key exchange"));
		ssl->state++;
		return (0);
	}
#if !defined(TROPICSSL_DHM_C)
	SSL_DEBUG_MSG(1, ("support for dhm in not available"));
	return (TROPICSSL_ERR_SSL_FEATURE_UNAVAILABLE);
#else
	if ((ret = ssl_read_handshake(ssl)) != 0) {
		SSL_DEBUG_RET(1, "ssl_read_handshake", ret);
		return (ret);
	}

	if (ssl->in_msgtype != SSL_MSG_HANDSHAKE) {
		SSL_DEBUG_MSG(1, ("bad server key exchange message"));
		return (TROPICSSL_ERR_SSL_UNEXPECTED_MESSAGE);
	}

	if (ssl->in_msg[0] != SSL_HS_SERVER_KEY_EXCHANGE) {
		SSL_DEBUG_MSG(1, ("bad server key exchange message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE);
	}

	/*
	 * Ephemeral DH parameters:
	 *
	 * struct {
	 *         opaque dh_p<1..2^16-1>;
	 *         opaque dh_g<1..2^16-1>;
	 *         opaque dh_Ys<1..2^16-1>;
	 * } ServerDHParams;
	 */
	doff = ssl->datagram ? 8 : 0;
	p = ssl->in_msg + doff + 4;
	// hslen already has doff+4
	end = ssl->in_msg + ssl->in_hslen;

	if ((ret = dhm_read_params(&ssl->dhm_ctx, &p, end)) != 0) {
		SSL_DEBUG_MSG(1, ("bad server key exchange message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE);
	}

	if ((int)(end - p) != ssl->peer_cert->rsa.len) {
		SSL_DEBUG_MSG(1, ("bad server key exchange message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE);
	}

	if (ssl->dhm_ctx.len < 64 || ssl->dhm_ctx.len > 256) {
		SSL_DEBUG_MSG(1, ("bad server key exchange message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE);
	}

	SSL_DEBUG_MPI(3, "DHM: P ", &ssl->dhm_ctx.P);
	SSL_DEBUG_MPI(3, "DHM: G ", &ssl->dhm_ctx.G);
	SSL_DEBUG_MPI(3, "DHM: GY", &ssl->dhm_ctx.GY);

	/*
	 * digitally-signed struct {
	 *         opaque md5_hash[16];
	 *         opaque sha_hash[20];
	 * };
	 *
	 * md5_hash
	 *         MD5(ClientHello.random + ServerHello.random
	 *                                                        + ServerParams);
	 * sha_hash
	 *         SHA(ClientHello.random + ServerHello.random
	 *                                                        + ServerParams);
	 */
	// sizeof(serverparams)
	n = ssl->in_hslen - (end - p) - 6 - doff;

	md5_starts(&md5);
	md5_update(&md5, ssl->randbytes, 64);
	md5_update(&md5, ssl->in_msg + doff + 4, n);
	md5_finish(&md5, hash);

	sha1_starts(&sha1);
	sha1_update(&sha1, ssl->randbytes, 64);
	sha1_update(&sha1, ssl->in_msg + doff + 4, n);
	sha1_finish(&sha1, hash + 16);

	SSL_DEBUG_BUF(3, "parameters hash", hash, 36);

	if ((ret = rsa_pkcs1_verify(&ssl->peer_cert->rsa, RSA_PUBLIC,
				    RSA_RAW, 36, hash, p)) != 0) {
		SSL_DEBUG_RET(1, "rsa_pkcs1_verify", ret);
		return (ret);
	}

	ssl->state++;

	SSL_DEBUG_MSG(2, ("<= parse server key exchange"));

	return (0);
#endif
}

static int ssl_parse_certificate_request(ssl_context * ssl)
{
	int ret;

	SSL_DEBUG_MSG(2, ("=> parse certificate request"));

	/*
	 *         0  .   0       handshake type
	 *         1  .   3       handshake length
	 *         4  .   5       SSL version
	 *         6  .   6       cert type count
	 *         7  .. n-1  cert types
	 *         n  .. n+1  length of all DNs
	 *        n+2 .. n+3  length of DN 1
	 *        n+4 .. ...  Distinguished Name #1
	 *        ... .. ...  length of DN 2, etc.
	 */
	if ((ret = ssl_read_handshake(ssl)) != 0) {
		SSL_DEBUG_RET(1, "ssl_read_handshake", ret);
		return (ret);
	}

	if (ssl->in_msgtype != SSL_MSG_HANDSHAKE) {
		SSL_DEBUG_MSG(1, ("bad certificate request message"));
		return (TROPICSSL_ERR_SSL_UNEXPECTED_MESSAGE);
	}

	ssl->client_auth = 0;
	ssl->state++;

	if (ssl->in_msg[0] == SSL_HS_CERTIFICATE_REQUEST)
		ssl->client_auth++;

	SSL_DEBUG_MSG(3, ("got %s certificate request",
			  ssl->client_auth ? "a" : "no"));

	SSL_DEBUG_MSG(2, ("<= parse certificate request"));

	return (0);
}

static int ssl_parse_server_hello_done(ssl_context * ssl)
{
	int ret;

	SSL_DEBUG_MSG(2, ("=> parse server hello done"));

	if (ssl->client_auth != 0) {
		if ((ret = ssl_read_handshake(ssl)) != 0) {
			SSL_DEBUG_RET(1, "ssl_read_handshake", ret);
			return (ret);
		}

		if (ssl->in_msgtype != SSL_MSG_HANDSHAKE) {
			SSL_DEBUG_MSG(1, ("bad server hello done message"));
			return (TROPICSSL_ERR_SSL_UNEXPECTED_MESSAGE);
		}
	}

	if (ssl->in_hslen != (ssl->datagram ? 4 + 8 : 4) || ssl->in_msg[0] != SSL_HS_SERVER_HELLO_DONE) {
		SSL_DEBUG_MSG(1, ("bad server hello done message"));
		return (TROPICSSL_ERR_SSL_BAD_HS_SERVER_HELLO_DONE);
	}

	ssl->state++;

	SSL_DEBUG_MSG(2, ("<= parse server hello done"));

	return (0);
}

static int ssl_write_client_key_exchange(ssl_context * ssl)
{
	int ret, i, n, doff;

	SSL_DEBUG_MSG(2, ("=> write client key exchange"));

	doff = ssl->datagram ? 8 : 0;

	if (ssl->session->cipher == SSL_EDH_RSA_DES_168_SHA ||
	    ssl->session->cipher == SSL_EDH_RSA_AES_256_SHA ||
	    ssl->session->cipher == SSL_EDH_RSA_CAMELLIA_256_SHA) {
#if !defined(TROPICSSL_DHM_C)
		SSL_DEBUG_MSG(1, ("support for dhm in not available"));
		return (TROPICSSL_ERR_SSL_FEATURE_UNAVAILABLE);
#else
		/*
		 * DHM key exchange -- send G^X mod P
		 */
		n = ssl->dhm_ctx.len;

		ssl->out_msg[4+doff] = (unsigned char)(n >> 8);
		ssl->out_msg[5+doff] = (unsigned char)(n);
		i = 6 + doff;

		ret = dhm_make_public(&ssl->dhm_ctx, 256,
				      &ssl->out_msg[i], n,
				      ssl->f_rng, ssl->p_rng);
		if (ret != 0) {
			SSL_DEBUG_RET(1, "dhm_make_public", ret);
			return (ret);
		}

		SSL_DEBUG_MPI(3, "DHM: X ", &ssl->dhm_ctx.X);
		SSL_DEBUG_MPI(3, "DHM: GX", &ssl->dhm_ctx.GX);

		ssl->pmslen = ssl->dhm_ctx.len;

		if ((ret = dhm_calc_secret(&ssl->dhm_ctx,
					   ssl->premaster,
					   &ssl->pmslen)) != 0) {
			SSL_DEBUG_RET(1, "dhm_calc_secret", ret);
			return (ret);
		}

		SSL_DEBUG_MPI(3, "DHM: K ", &ssl->dhm_ctx.K);
#endif
	} else {
		/*
		 * RSA key exchange -- send rsa_public(pkcs1 v1.5(premaster))
		 */
		ssl->premaster[0] = (unsigned char)ssl->max_major_ver;
		ssl->premaster[1] = (unsigned char)ssl->max_minor_ver;
		ssl->pmslen = 48;

		for (i = 2; i < ssl->pmslen; i++)
			ssl->premaster[i] =
			    (unsigned char)ssl->f_rng(ssl->p_rng);

		i = 4 + doff;
		n = ssl->peer_cert->rsa.len;

		if (ssl->minor_ver != SSL_MINOR_VERSION_0) {
			i += 2;
			ssl->out_msg[4] = (unsigned char)(n >> 8);
			ssl->out_msg[5] = (unsigned char)(n);
		}

		ret = rsa_pkcs1_encrypt(&ssl->peer_cert->rsa, RSA_PUBLIC,
					ssl->pmslen, ssl->premaster,
					ssl->out_msg + i);
		if (ret != 0) {
			SSL_DEBUG_RET(1, "rsa_pkcs1_encrypt", ret);
			return (ret);
		}
	}

	ssl_derive_keys(ssl);

	ssl->out_msglen = i + n;
	ssl->out_msgtype = SSL_MSG_HANDSHAKE;
	ssl->out_msg[0] = SSL_HS_CLIENT_KEY_EXCHANGE;

	ssl->state++;

	if ((ret = ssl_write_record(ssl)) != 0) {
		SSL_DEBUG_RET(1, "ssl_write_record", ret);
		return (ret);
	}

	SSL_DEBUG_MSG(2, ("<= write client key exchange"));

	return (0);
}

static int ssl_write_certificate_verify(ssl_context * ssl)
{
	int ret, n;
	unsigned char hash[36];

	SSL_DEBUG_MSG(2, ("=> write certificate verify"));

	if (ssl->client_auth == 0) {
		SSL_DEBUG_MSG(2, ("<= skip write certificate verify"));
		ssl->state++;
		return (0);
	}

	if (ssl->rsa_key == NULL) {
		SSL_DEBUG_MSG(1, ("got no private key"));
		return (TROPICSSL_ERR_SSL_PRIVATE_KEY_REQUIRED);
	}

	/*
	 * Make an RSA signature of the handshake digests
	 */
	ssl_calc_verify(ssl, hash);

	n = ssl->rsa_key->len;
	ssl->out_msg[4] = (unsigned char)(n >> 8);
	ssl->out_msg[5] = (unsigned char)(n);

	if ((ret = rsa_pkcs1_sign(ssl->rsa_key, RSA_PRIVATE, RSA_RAW,
				  36, hash, ssl->out_msg + 6)) != 0) {
		SSL_DEBUG_RET(1, "rsa_pkcs1_sign", ret);
		return (ret);
	}

	ssl->out_msglen = 6 + n;
	ssl->out_msgtype = SSL_MSG_HANDSHAKE;
	ssl->out_msg[0] = SSL_HS_CERTIFICATE_VERIFY;

	ssl->state++;

	if ((ret = ssl_write_record(ssl)) != 0) {
		SSL_DEBUG_RET(1, "ssl_write_record", ret);
		return (ret);
	}

	SSL_DEBUG_MSG(2, ("<= write certificate verify"));

	return (0);
}

/*
 * SSL handshake -- client side
 */
int ssl_handshake_client(ssl_context * ssl)
{
	int ret = 0;

	SSL_DEBUG_MSG(2, ("=> handshake client"));

	while (ssl->state != SSL_HANDSHAKE_OVER) {
		SSL_DEBUG_MSG(2, ("client state: %d", ssl->state));

		if ((ret = ssl_flush_output(ssl)) != 0)
			break;

		switch (ssl->state) {
		case SSL_HELLO_REQUEST:
			ssl->state = SSL_CLIENT_HELLO;
			break;

			/*
			 *      ==>       ClientHello
			 */
		case SSL_CLIENT_HELLO:
			ret = ssl_write_client_hello(ssl);
			break;
		case SSL_SERVER_HELLOVERIFY:
			ret = ssl_parse_server_helloverify(ssl);
			break;
			/*
			 *      <==       ServerHello
			 *                Certificate
			 *              ( ServerKeyExchange      )
			 *              ( CertificateRequest )
			 *                ServerHelloDone
			 */
		case SSL_SERVER_HELLO:
			ret = ssl_parse_server_hello(ssl);
			break;

		case SSL_SERVER_CERTIFICATE:
			ret = ssl_parse_certificate(ssl);
			break;

		case SSL_SERVER_KEY_EXCHANGE:
			ret = ssl_parse_server_key_exchange(ssl);
			break;

		case SSL_CERTIFICATE_REQUEST:
			ret = ssl_parse_certificate_request(ssl);
			break;

		case SSL_SERVER_HELLO_DONE:
			ret = ssl_parse_server_hello_done(ssl);
			break;

			/*
			 *      ==> ( Certificate/Alert  )
			 *                ClientKeyExchange
			 *              ( CertificateVerify      )
			 *                ChangeCipherSpec
			 *                Finished
			 */
		case SSL_CLIENT_CERTIFICATE:
			ret = ssl_write_certificate(ssl);
			break;

		case SSL_CLIENT_KEY_EXCHANGE:
			ret = ssl_write_client_key_exchange(ssl);
			break;

		case SSL_CERTIFICATE_VERIFY:
			ret = ssl_write_certificate_verify(ssl);
			break;

		case SSL_CLIENT_CHANGE_CIPHER_SPEC:
			ret = ssl_write_change_cipher_spec(ssl);
			break;

		case SSL_CLIENT_FINISHED:
			ret = ssl_write_finished(ssl);
			break;

			/*
			 *      <==       ChangeCipherSpec
			 *                Finished
			 */
		case SSL_SERVER_CHANGE_CIPHER_SPEC:
			ret = ssl_parse_change_cipher_spec(ssl);
			break;

		case SSL_SERVER_FINISHED:
			ret = ssl_parse_finished(ssl);
			break;

		case SSL_FLUSH_BUFFERS:
			SSL_DEBUG_MSG(2, ("handshake: done"));
			ssl->state = SSL_HANDSHAKE_OVER;
			break;

		default:
			SSL_DEBUG_MSG(1, ("invalid state %d", ssl->state));
			return (TROPICSSL_ERR_SSL_BAD_INPUT_DATA);
		}

		if (ret != 0)
			break;
	}

	SSL_DEBUG_MSG(2, ("<= handshake client"));

	return (ret);
}

#endif
