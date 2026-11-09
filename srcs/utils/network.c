#include "network.h"
#include "buffers.h"
#include "tls.h"

#include <errno.h>

static int family;

static void	network_fail(t_syschat *syschat, int conn_err, int fatal)
{
	if (fatal)
		error_exit(syschat, conn_err);
}

static int	network_connect_plain(t_syschat *syschat, t_conn *conn, int fatal)
{
	char				ip[INET6_ADDRSTRLEN];
	struct sockaddr_in	v4;
	struct sockaddr_in6	v6;

	bzero(ip, INET6_ADDRSTRLEN);
	if (network_get_ip(conn, ip) == -1)
	{
		network_fail(syschat, 4, fatal);
		return (-1);
	}
	if ((conn->fd = socket(family, SOCK_STREAM, 0)) < 0)
	{
		conn->fd = -1;
		network_fail(syschat, 5, fatal);
		return (-1);
	}
	if (family == AF_INET)
	{
		bzero(&v4, sizeof(v4));
		v4.sin_family = AF_INET;
		v4.sin_port = htons(conn->port);
		if (inet_pton(family, ip, &v4.sin_addr) <= 0
			|| connect(conn->fd, (struct sockaddr *) &v4, sizeof(v4)))
		{
			close(conn->fd);
			conn->fd = -1;
			network_fail(syschat, 7, fatal);
			return (-1);
		}
	}
	else
	{
		bzero(&v6, sizeof(v6));
		v6.sin6_family = AF_INET6;
		v6.sin6_port = htons(conn->port);
		if (inet_pton(family, ip, &v6.sin6_addr) <= 0
			|| connect(conn->fd, (struct sockaddr *) &v6, sizeof(v6)))
		{
			close(conn->fd);
			conn->fd = -1;
			network_fail(syschat, 7, fatal);
			return (-1);
		}
	}
	return (0);
}

/*
** `fatal` keeps the historic behaviour for the server given on the command
** line (a bad host is an error message and exit 4/5/7) while a server added
** with /connect only loses its own slot.
*/
int	network_connect_server(t_syschat *syschat, t_conn *conn, int fatal)
{
	char	reason[256];
	char	line[512];
	int		index;

	conn->fd = -1;
	if (network_connect_plain(syschat, conn, fatal) == -1)
		return (-1);
	if (!conn->tls)
		return (0);
	if (tls_connect(conn, reason, sizeof(reason)) == 0)
		return (0);
	/*
	** --tls-required: no plaintext, ever. Refusing here is what turns the
	** handshake into a real guard instead of a hint: an attacker who cuts
	** the handshake gains nothing.
	*/
	if (conn->tls_required)
		error_tls_exit(syschat, reason);
	/*
	** A TLS failure must never lock the user out of IRC: fall back to the
	** plaintext connection used before this feature, and say so in clear
	** so nobody believes the session is encrypted when it is not.
	*/
	if (conn->fd >= 0)
		close(conn->fd);
	conn->fd = -1;
	if (network_connect_plain(syschat, conn, fatal) == -1)
		return (-1);
	bzero(line, sizeof(line));
	snprintf(line, sizeof(line),
		"\e[0;31mTLS: handshake failed (%s) -- falling back to plaintext, "
		"this session is NOT encrypted\e[0m", reason);
	index = buffers_find_on(syschat, (unsigned) (conn - syschat->conns),
		syschat_conn_name(conn));
	if (index == -1)
		index = syschat->active;
	buffers_append(syschat, index, line);
	return (0);
}

int	net_send(t_conn *conn, const char *buf, size_t len)
{
	if (!conn || conn->fd < 0)
		return (-1);
	if (conn->tls_active)
		return (tls_send(conn, buf, len));
	return (send(conn->fd, buf, len, MSG_DONTWAIT));
}

int	net_recv(t_conn *conn, char *buf, size_t len)
{
	ssize_t	ret;

	if (!conn || conn->fd < 0)
		return (-2);
	if (conn->tls_active)
		return (tls_recv(conn, buf, len));
	ret = recv(conn->fd, buf, len, 0);
	if (ret >= 0)
		return ((int) ret);
	if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
		return (-1);
	return (-2);
}

int	net_pending(t_conn *conn)
{
	if (!conn || conn->fd < 0)
		return (0);
	if (conn->tls_active)
		return (tls_pending(conn));
	return (0);
}

int network_get_ip(t_conn *conn, char *ip)
{
	void *addr = NULL;
	struct addrinfo hints, *res, *p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (!conn || !conn->hostname)
		return -1;
	if (getaddrinfo(conn->hostname, NULL, &hints, &res) != 0)
		return -1;
	p = res;

	if (p->ai_family == AF_INET)
		addr = &((struct sockaddr_in *)p->ai_addr)->sin_addr;
	else
		addr = &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr;

	family = p->ai_family;
	inet_ntop(p->ai_family, addr, ip, INET6_ADDRSTRLEN);
	freeaddrinfo(res);

	return (0);
}
