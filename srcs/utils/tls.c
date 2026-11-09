#include "tls.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "buffers.h"

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"

/* Handshake budget: a server that answers TCP but never speaks TLS must not
** freeze the client forever, and the fallback has to stay reachable. */
#define TLS_HANDSHAKE_TIMEOUT_MS 20000
/* Retry budget for a short SSL_write that returns WANT_WRITE. */
#define TLS_SEND_TIMEOUT_MS 200
#define TLS_MAX_FRAGMENT 16384

static const char *const tls_ca_paths[] = {
	"/etc/ssl/certs/ca-certificates.crt",
	"/etc/ssl/cert.pem",
	"/usr/local/etc/openssl/cert.pem",
	NULL
};

typedef struct s_tls_bio
{
	int	fd;
	int	block_ms;
} t_tls_bio;

typedef struct s_tls_state
{
	mbedtls_ssl_context			ssl;
	mbedtls_ssl_config			conf;
	mbedtls_entropy_context		entropy;
	mbedtls_ctr_drbg_context	drbg;
	mbedtls_x509_crt			ca;
	t_tls_bio					bio;
	int							ca_loaded;
} t_tls_state;

static int	tls_wait(int fd, short events, int timeout_ms)
{
	struct pollfd	pfd;
	int				ret;

	pfd.fd = fd;
	pfd.events = events;
	pfd.revents = 0;
	ret = poll(&pfd, 1, timeout_ms);
	if (ret < 0 && errno == EINTR)
		return (tls_wait(fd, events, timeout_ms));
	return (ret);
}

static int	tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
	t_tls_bio	*bio = ctx;
	ssize_t		ret;

	if (len > TLS_MAX_FRAGMENT)
		len = TLS_MAX_FRAGMENT;
	for (;;)
	{
		ret = send(bio->fd, buf, len, MSG_NOSIGNAL);
		if (ret >= 0)
			return ((int) ret);
		if (errno == EINTR)
			continue ;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			return (MBEDTLS_ERR_NET_SEND_FAILED);
		if (bio->block_ms <= 0)
			return (MBEDTLS_ERR_SSL_WANT_WRITE);
		if (tls_wait(bio->fd, POLLOUT, bio->block_ms) <= 0)
			return (MBEDTLS_ERR_SSL_TIMEOUT);
	}
}

static int	tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
	t_tls_bio	*bio = ctx;
	ssize_t		ret;

	if (len > TLS_MAX_FRAGMENT)
		len = TLS_MAX_FRAGMENT;
	for (;;)
	{
		ret = recv(bio->fd, buf, len, 0);
		if (ret >= 0)
			return ((int) ret);
		if (errno == EINTR)
			continue ;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			return (MBEDTLS_ERR_NET_RECV_FAILED);
		if (bio->block_ms <= 0)
			return (MBEDTLS_ERR_SSL_WANT_READ);
		if (tls_wait(bio->fd, POLLIN, bio->block_ms) <= 0)
			return (MBEDTLS_ERR_SSL_TIMEOUT);
	}
}

static void	tls_set_blocking(int fd, int blocking)
{
	int	flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return ;
	if (blocking)
		fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	else
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void	tls_state_free(t_tls_state *st)
{
	if (!st)
		return ;
	mbedtls_ssl_free(&st->ssl);
	mbedtls_ssl_config_free(&st->conf);
	mbedtls_x509_crt_free(&st->ca);
	mbedtls_ctr_drbg_free(&st->drbg);
	mbedtls_entropy_free(&st->entropy);
	free(st);
}

static void	tls_reason(t_tls_state *st, int ret, char *err, size_t errlen)
{
	char	detail[128];

	detail[0] = '\0';
	mbedtls_strerror(ret, detail, sizeof(detail));
	if (detail[0] == '\0')
		snprintf(err, errlen, "mbedTLS -0x%04x", -ret);
	else
		snprintf(err, errlen, "%s (mbedTLS -0x%04x)", detail, -ret);
	(void) st;
}

int	tls_connect(t_conn *conn, char *err, size_t errlen)
{
	t_tls_state	*st;
	const char	*const *path;
	int			ret;

	st = calloc(1, sizeof(t_tls_state));
	if (!st)
		return (snprintf(err, errlen, "out of memory"), -1);
	mbedtls_ssl_init(&st->ssl);
	mbedtls_ssl_config_init(&st->conf);
	mbedtls_entropy_init(&st->entropy);
	mbedtls_ctr_drbg_init(&st->drbg);
	mbedtls_x509_crt_init(&st->ca);

	if ((ret = mbedtls_ctr_drbg_seed(&st->drbg, mbedtls_entropy_func,
				&st->entropy, NULL, 0)) != 0)
	{
		tls_reason(st, ret, err, errlen);
		tls_state_free(st);
		return (-1);
	}
	for (path = tls_ca_paths; *path && !st->ca_loaded; path++)
	{
		if (access(*path, R_OK) != 0)
			continue ;
		if (mbedtls_x509_crt_parse_file(&st->ca, *path) == 0)
			st->ca_loaded = 1;
	}
	if (!st->ca_loaded && !conn->tls_insecure)
	{
		snprintf(err, errlen, "no CA bundle found (%s), use --insecure",
			tls_ca_paths[0]);
		tls_state_free(st);
		return (-1);
	}
	if ((ret = mbedtls_ssl_config_defaults(&st->conf, MBEDTLS_SSL_IS_CLIENT,
				MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		tls_reason(st, ret, err, errlen);
		tls_state_free(st);
		return (-1);
	}
	mbedtls_ssl_conf_authmode(&st->conf, st->ca_loaded && !conn->tls_insecure
		? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
	if (st->ca_loaded)
		mbedtls_ssl_conf_ca_chain(&st->conf, &st->ca, NULL);
	mbedtls_ssl_conf_rng(&st->conf, mbedtls_ctr_drbg_random, &st->drbg);
	if ((ret = mbedtls_ssl_setup(&st->ssl, &st->conf)) != 0)
	{
		tls_reason(st, ret, err, errlen);
		tls_state_free(st);
		return (-1);
	}
	if ((ret = mbedtls_ssl_set_hostname(&st->ssl, conn->hostname)) != 0)
	{
		tls_reason(st, ret, err, errlen);
		tls_state_free(st);
		return (-1);
	}
	st->bio.fd = conn->fd;
	st->bio.block_ms = TLS_HANDSHAKE_TIMEOUT_MS;
	mbedtls_ssl_set_bio(&st->ssl, &st->bio, tls_bio_send, tls_bio_recv, NULL);
	/* Non-blocking from here on: the main loop keeps owning the readiness
	** checks through poll(), and tls_bio_recv never waits after handshake. */
	tls_set_blocking(conn->fd, 0);

	while ((ret = mbedtls_ssl_handshake(&st->ssl)) != 0)
	{
		if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue ;
		tls_reason(st, ret, err, errlen);
		tls_set_blocking(conn->fd, 1);
		tls_state_free(st);
		return (-1);
	}
	if (mbedtls_ssl_get_verify_result(&st->ssl) != 0 && !conn->tls_insecure)
	{
		uint32_t	flags;

		flags = mbedtls_ssl_get_verify_result(&st->ssl);
		mbedtls_x509_crt_verify_info(err, errlen, "", flags);
		if (err[0] == '\0')
			snprintf(err, errlen, "certificate verification failed");
		tls_set_blocking(conn->fd, 1);
		tls_state_free(st);
		return (-1);
	}
	st->bio.block_ms = 0;
	conn->tls_state = st;
	conn->tls_active = 1;
	return (0);
}

int	tls_pending(t_conn *conn)
{
	t_tls_state	*st;

	st = conn->tls_state;
	if (!st || !conn->tls_active)
		return (0);
	return (mbedtls_ssl_get_bytes_avail(&st->ssl) > 0);
}

/*
** A dropped line used to be invisible: every caller ignores the return value,
** so a short SSL_write looked exactly like a delivered message.
*/
static void	tls_report_drop(t_conn *conn, int ret)
{
	char	line[192];

	snprintf(line, sizeof(line),
		"\e[0;31mTLS: message dropped, write failed (mbedTLS -0x%04x)\e[0m", -ret);
	if (conn->owner && conn->owner->active >= 0
		&& conn->owner->active < conn->owner->buffer_count)
		buffers_append(conn->owner, conn->owner->active, line);
	else
		fprintf(stderr, "%s\n", line);
}

int	tls_send(t_conn *conn, const char *buf, size_t len)
{
	t_tls_state	*st;
	int			ret;
	int			saved;

	st = conn->tls_state;
	if (!st)
		return (-1);
	saved = st->bio.block_ms;
	st->bio.block_ms = TLS_SEND_TIMEOUT_MS;
	ret = mbedtls_ssl_write(&st->ssl, (const unsigned char *) buf, len);
	if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ)
		ret = mbedtls_ssl_write(&st->ssl, (const unsigned char *) buf, len);
	st->bio.block_ms = saved;
	if (ret < 0)
	{
		tls_report_drop(conn, ret);
		return (-1);
	}
	return (ret);
}

int	tls_recv(t_conn *conn, char *buf, size_t len)
{
	t_tls_state	*st;
	int			ret;

	st = conn->tls_state;
	if (!st)
		return (-2);
	ret = mbedtls_ssl_read(&st->ssl, (unsigned char *) buf, len);
	if (ret > 0)
		return (ret);
	if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY
		|| ret == MBEDTLS_ERR_SSL_CONN_EOF)
		return (0);
	if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
		return (-1);
	return (-2);
}

void	tls_close(t_conn *conn)
{
	t_tls_state	*st;

	st = conn->tls_state;
	conn->tls_state = NULL;
	conn->tls_active = 0;
	if (!st)
		return ;
	if (conn->fd >= 0)
		mbedtls_ssl_close_notify(&st->ssl);
	tls_state_free(st);
}

void	tls_summary(t_conn *conn, char *buf, size_t len)
{
	t_tls_state	*st;

	st = conn->tls_state;
	if (!st || !conn->tls_active)
	{
		snprintf(buf, len, "plaintext, no encryption");
		return ;
	}
	snprintf(buf, len, "%s, %s, cert %s", mbedtls_ssl_get_version(&st->ssl),
		mbedtls_ssl_get_ciphersuite(&st->ssl),
		conn->tls_insecure ? "NOT verified" : "verified");
}
