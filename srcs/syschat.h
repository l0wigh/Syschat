#ifndef SYSCHAT_H
# define SYSCHAT_H

#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200809L
/*
** Raw mode is entered before the connection is attempted, so a fatal error
** during connect must be able to put the terminal back: without this the
** shell is left with ECHO off and the user types blind.
*/
void	syschat_terminal_raw(void);
void	syschat_terminal_restore(void);

#endif
#ifndef _DEFAULT_SOURCE
# define _DEFAULT_SOURCE
/*
** Raw mode is entered before the connection is attempted, so a fatal error
** during connect must be able to put the terminal back: without this the
** shell is left with ECHO off and the user types blind.
*/
void	syschat_terminal_raw(void);
void	syschat_terminal_restore(void);

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
#include <time.h>

#define BF_SIZE 512
#define SYSCHAT_QUIT "Syschat 0.4.2-RC1"
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
	int						net_socket;
	int						port;
	/*
	** TLS is opt-in (port 6697 or --tls) and never on by default on 6667.
	** `tls_state` stays opaque: only utils/tls.c includes the mbedTLS
	** headers, every other translation unit just sees an int.
	*/
	int						tls;
	int						tls_active;
	int						tls_insecure;
	int						tls_required;
	void					*tls_state;
	int					listing;
	time_t				listing_since;
	int					list_received;
	int					elist_mask;
	char				list_filter[SYSCHAT_NAME_LEN];
	struct sockaddr_in	v4_socket;
	struct sockaddr_in6	v6_socket;
	int					running;
} t_syschat;

/*
** Raw mode is entered before the connection is attempted, so a fatal error
** during connect must be able to put the terminal back: without this the
** shell is left with ECHO off and the user types blind.
*/
void	syschat_terminal_raw(void);
void	syschat_terminal_restore(void);

#endif
