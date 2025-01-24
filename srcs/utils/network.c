#include "network.h"

static int family;

void network_connect_server(t_syschat *syschat)
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
		syschat->v4_socket.sin_port = htons(6667);
		if (inet_pton(family, ip, &syschat->v4_socket.sin_addr) <= 0)
			error_exit(syschat, 6);
		if (connect(syschat->net_socket, (struct sockaddr *) &syschat->v4_socket, sizeof(syschat->v4_socket)))
			error_exit(syschat, 7);
	}
	else
	{
		syschat->v6_socket.sin6_family = AF_INET6;
		syschat->v6_socket.sin6_port = htons(6667);
		if (inet_pton(family, ip, &syschat->v6_socket.sin6_addr) <= 0)
			error_exit(syschat, 6);
		if (connect(syschat->net_socket, (struct sockaddr *) &syschat->v6_socket, sizeof(syschat->v6_socket)))
			error_exit(syschat, 7);
	}

	printf("Connected to %s as %s\n", syschat->hostname, syschat->nickname);
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

	return 0;
}
