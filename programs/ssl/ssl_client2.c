/*
 *	SSL client with certificate authentication
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

#ifndef _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE 1
#endif

#include <string.h>
#include <stdio.h>

#include "tropicssl/net.h"
#include "tropicssl/ssl.h"
#include "tropicssl/havege.h"
#include "tropicssl/certs.h"
#include "tropicssl/x509.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "../pcapif.h"
#include "../lwipcfg_msvc.h"

#define SERVER_PORT 4443
#define SERVER_NAME "192.168.1.3"
#define GET_REQUEST "GET / HTTP/1.0\r\n\r\n"
/*
  #define SERVER_NAME "tropicssl.org"
  #define GET_REQUEST \
  "GET /hello/ HTTP/1.1\r\n" \
  "Host: tropicssl.org\r\n\r\n"
*/

void ssl_debug(void *ctx, int level, char *str)
{
	if(str[strlen(str)-1] == '\n')
		printf("[%4d] %s", sys_now(), str);
	else 
		printf("%s", str);

	fflush(stdout);
	(void)ctx;
}

static struct netif	 pcapif;
static unsigned char debug_flags;
static ip_addr_t	 ip, netmask, gw; /* (manually) host IP configuration */
static sys_sem_t	 init_sem;

static void tcpip_init_done (void *arg)
{
	sys_sem_t *sem = (sys_sem_t*)arg;
	if(netif_add(&pcapif, &ip, &netmask, &gw, NULL, pcapif_init, tcpip_input) == NULL)
	{
		printf("netif_add failed\n");
		exit(-1);
	}

	netif_set_default(&pcapif);
	netif_set_up(&pcapif);

	sys_sem_signal(sem);
}

#define DIR "c:\\Users\\me\\src\\mgo\\certs\\"
int main(void)
{
	int ret, len, server_fd;
	unsigned char buf[1024];
	havege_state hs;
	ssl_context ssl;
	ssl_session ssn;
	x509_cert cacert;
	x509_cert clicert;
	rsa_context rsa;

	LWIP_PORT_INIT_GW(&gw);
	LWIP_PORT_INIT_NETMASK(&netmask);
	LWIP_PORT_INIT_IPADDR(&ip);

	sys_init();
	sys_sem_new(&init_sem, 0);
	tcpip_init(tcpip_init_done, &init_sem);
	sys_sem_wait(&init_sem);

	/*
	 * 0. Initialize the RNG and the session data
	 */
	havege_init(&hs);
	memset(&ssn, 0, sizeof(ssl_session));

	/*
	 * 1.1. Load the trusted CA
	 */
	printf("\n	. Loading the CA root certificate ...");
	fflush(stdout);

	memset(&cacert, 0, sizeof(x509_cert));

	/*
	 * Alternatively, you may load the CA certificates from a .pem or
	 * .crt file by calling x509parse_crtfile( &cacert, "myca.crt" ).
	 */
	ret = x509parse_crtfile(&cacert, DIR"x509-ca.pem");
	if (ret != 0) {
		printf(" failed\n  !  x509parse_crt returned %d\n\n", ret);
		goto exit;
	}

	printf(" ok\n");

	/*
	 * 1.2. Load own certificate and private key
	 *
	 * (can be skipped if client authentication is not required)
	 */
	printf("  . Loading the client cert. and key...");
	fflush(stdout);

	memset(&clicert, 0, sizeof(x509_cert));

	ret = x509parse_crtfile(&clicert, DIR"x509-client.pem");
	if (ret != 0) {
		printf(" failed\n  !  x509parse_crt returned %d\n\n", ret);
		goto exit;
	}

	ret = x509parse_keyfile(&rsa, DIR"x509-client-key.pem", NULL);
	if (ret != 0) {
		printf(" failed\n  !  x509parse_key returned %d\n\n", ret);
		goto exit;
	}

	printf(" ok\n");

	/*
	 * 2. Start the connection
	 */
	printf("  . Connecting to tcp/%s/%-4d...", SERVER_NAME, SERVER_PORT);
	fflush(stdout);

	if ((ret = net_connect(&server_fd, SERVER_NAME, SERVER_PORT)) != 0) {
		printf(" failed\n  ! net_connect returned %d\n\n", ret);
		goto exit;
	}

	printf(" ok\n");

	/*
	 * 3. Setup stuff
	 */
	printf("  . Setting up the SSL/TLS structure...");
	fflush(stdout);

	if ((ret = ssl_init(&ssl)) != 0) {
		printf(" failed\n  ! ssl_init returned %d\n\n", ret);
		goto exit;
	}

	printf(" ok\n");

	ssl_set_dbg(&ssl, ssl_debug, NULL);
	ssl_set_endpoint(&ssl, SSL_IS_CLIENT);
	ssl_set_authmode(&ssl, SSL_VERIFY_OPTIONAL);

	ssl_set_rng(&ssl, havege_rand, &hs);
	ssl_set_bio(&ssl, net_recv, &server_fd, net_send, &server_fd);

	ssl_set_ciphers(&ssl, ssl_default_ciphers);
	ssl_set_session(&ssl, 1, 600, &ssn);

	ssl_set_ca_chain(&ssl, &cacert, SERVER_NAME);
	ssl_set_own_cert(&ssl, &clicert, &rsa);

	ssl_set_hostname(&ssl, SERVER_NAME);

	/*
	 * 4. Handshake
	 */
	printf("  . Performing the SSL/TLS handshake...");
	fflush(stdout);

	while ((ret = ssl_handshake(&ssl)) != 0) {
		if (ret != TROPICSSL_ERR_NET_TRY_AGAIN) {
			printf(" failed\n  ! ssl_handshake returned %d\n\n", ret);
			goto exit;
		}
	}

	printf(" ok\n	 [ Cipher is %s ]\n", ssl_get_cipher(&ssl));

	/*
	 * 5. Verify the server certificate
	 */
	printf("  . Verifying peer X.509 certificate...");

	if ((ret = ssl_get_verify_result(&ssl)) != 0) {
		printf(" failed\n");

		if ((ret & BADCERT_EXPIRED) != 0)
			printf("  ! server certificate has expired\n");

		if ((ret & BADCERT_REVOKED) != 0)
			printf("  ! server certificate has been revoked\n");

		if ((ret & BADCERT_CN_MISMATCH) != 0)
			printf("  ! CN mismatch (expected CN=%s)\n",
				   SERVER_NAME);

		if ((ret & BADCERT_NOT_TRUSTED) != 0)
			printf
				("	! self-signed or not signed by a trusted CA\n");

		printf("\n");
	} else
		printf(" ok\n");

	printf("  . Peer certificate information	...\n");
	printf(x509parse_cert_info("	  ", ssl.peer_cert));

	/*
	 * 6. Write the GET request
	 */
	printf("  > Write to server:");
	fflush(stdout);

	len = sprintf((char *)buf, GET_REQUEST);

	while ((ret = ssl_write(&ssl, buf, len)) <= 0) {
		if (ret != TROPICSSL_ERR_NET_TRY_AGAIN) {
			printf(" failed\n  ! ssl_write returned %d\n\n", ret);
			goto exit;
		}
	}

	len = ret;
	printf(" %d bytes written\n\n%s", len, (char *)buf);

	/*
	 * 7. Read the HTTP response
	 */
	printf("  < Read from server:");
	fflush(stdout);

	do {
		len = sizeof(buf) - 1;
		memset(buf, 0, sizeof(buf));
		ret = ssl_read(&ssl, buf, len);

		if (ret == TROPICSSL_ERR_NET_TRY_AGAIN)
			continue;

		if (ret == TROPICSSL_ERR_SSL_PEER_CLOSE_NOTIFY)
			break;

		if (ret <= 0) {
			printf("failed\n  ! ssl_read returned %d\n\n", ret);
			break;
		}

		len = ret;
		printf(" %d bytes read\n\n%s", len, (char *)buf);
	}
	while (0);

	ssl_close_notify(&ssl);

exit:

	net_close(server_fd);
	x509_free(&clicert);
	x509_free(&cacert);
	rsa_free(&rsa);
	ssl_free(&ssl);

	memset(&ssl, 0, sizeof(ssl));

#ifdef WIN32
	printf("  + Press Enter to exit this program.\n");
	fflush(stdout);
	getchar();
#endif

	return (ret);
}
