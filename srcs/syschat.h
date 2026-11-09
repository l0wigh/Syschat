#ifndef SYSCHAT_H
# define SYSCHAT_H

#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
# define _DEFAULT_SOURCE
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

#define BF_SIZE 512
#define SYSCHAT_QUIT "Syschat 0.4.1-RC1"
#define SYSCHAT_USERINFO "You want that sweet sweet Syschat client ? https://github.com/L0Wigh/syschat"

#define SYSCHAT_MAX_BUFFERS 16
#define SYSCHAT_NAME_LEN 64
#define SYSCHAT_HISTORY 256
#define SYSCHAT_LINE_LEN 1024

/*
** One buffer = one tab. `name` is either "#channel", a nickname (private
** message) or the server hostname (first buffer, always index 0).
** `lines` is a ring buffer of already coloured lines; nothing is ever
** dropped unless the ring wraps (SYSCHAT_HISTORY lines of scrollback).
*/
typedef struct s_buffer
{
	char	*name;
	char	*lines[SYSCHAT_HISTORY];
	int		head;
	int		count;
	int		unread;
	int		joined;
} t_buffer;

typedef struct s_syschat
{
	char				*hostname;
	char				*nickname;
	t_buffer			buffers[SYSCHAT_MAX_BUFFERS];
	int					buffer_count;
	int					active;
	int					net_socket;
	int					port;
	struct sockaddr_in	v4_socket;
	struct sockaddr_in6	v6_socket;
	int					running;
} t_syschat;

#endif
