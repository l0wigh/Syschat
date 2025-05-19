#ifndef SYSCHAT_H
# define SYSCHAT_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>

typedef struct s_syschat
{
	char				*hostname;
	char				*nickname;
	char				*channel;
	int					net_socket;
	struct sockaddr_in	v4_socket;
	struct sockaddr_in6 v6_socket;
	int					epoll_fd;
	struct epoll_event	epoll_event;
	int					running;
} t_syschat;

#define BF_SIZE 512
#define SYSCHAT_QUIT "Syschat 0.3.2"

#endif
