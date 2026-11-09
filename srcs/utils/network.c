#include "network.h"
#include "buffers.h"
#include "tls.h"

#include <errno.h>

static int family;

static void	network_connect_plain(t_syschat *syschat)
{
	char ip[INET6_ADDRSTRLEN];

	bzero(ip, INET6_ADDRSTRLEN);
	if (network_get_ip(syschat, ip) == -1)
		error_exit(syschat, 4);

	if ((syschat->net_socket = socket(family, SOCK_STREAM, 0)) < 0)
		error_exit(syschat, 5);

	if (family == AF_INET)
	{
		syschat->v4_socket.sin_family = AF_INET;
		syschat->v4_socket.sin_port = htons(syschat->port);
		if (inet_pton(family, ip, &syschat->v4_socket.sin_addr) <= 0)
			error_exit(syschat, 6);
		if (connect(syschat->net_socket, (struct sockaddr *) &syschat->v4_socket, sizeof(syschat->v4_socket)))
			error_exit(syschat, 7);
	}
	else
	{
		syschat->v6_socket.sin6_family = AF_INET6;
		syschat->v6_socket.sin6_port = htons(syschat->port);
		if (inet_pton(family, ip, &syschat->v6_socket.sin6_addr) <= 0)
			error_exit(syschat, 6);
		if (connect(syschat->net_socket, (struct sockaddr *) &syschat->v6_socket, sizeof(syschat->v6_socket)))
			error_exit(syschat, 7);
	}
}

void	network_connect_server(t_syschat *syschat)
{
	char	reason[256];
	char	line[512];

	network_connect_plain(syschat);
	if (!syschat->tls)
		return ;
	if (tls_connect(syschat, reason, sizeof(reason)) == 0)
		return ;
	/*
	** A TLS failure must never lock the user out of IRC: fall back to the
	** plaintext connection used before this feature, and say so in clear
	** so nobody believes the session is encrypted when it is not.
	*/
	close(syschat->net_socket);
	syschat->net_socket = 0;
	network_connect_plain(syschat);
	bzero(line, sizeof(line));
	snprintf(line, sizeof(line),
		"\e[0;31mTLS: handshake failed (%s) -- falling back to plaintext, "
		"this session is NOT encrypted\e[0m", reason);
	buffers_append(syschat, 0, line);
}

int	net_send(t_syschat *syschat, const char *buf, size_t len)
{
	if (syschat->tls_active)
		return (tls_send(syschat, buf, len));
	return (send(syschat->net_socket, buf, len, MSG_DONTWAIT));
}

int	net_recv(t_syschat *syschat, char *buf, size_t len)
{
	ssize_t	ret;

	if (syschat->tls_active)
		return (tls_recv(syschat, buf, len));
	ret = recv(syschat->net_socket, buf, len, 0);
	if (ret >= 0)
		return ((int) ret);
	if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
		return (-1);
	return (-2);
}

int	net_pending(t_syschat *syschat)
{
	if (syschat->tls_active)
		return (tls_pending(syschat));
	return (0);
}

int network_get_ip(t_syschat *syschat, char *ip)
{
	void *addr = NULL;
	struct addrinfo hints, *res, *p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(syschat->hostname, NULL, &hints, &res) != 0)
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
