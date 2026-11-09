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
#define SYSCHAT_MAX_SERVERS 4
#define SYSCHAT_NAME_LEN 64
#define SYSCHAT_HISTORY 256
#define SYSCHAT_LINE_LEN 1024
#define SYSCHAT_NET_BUFFER 16384
#define SYSCHAT_LIST_BUFFER "Liste"
#define SYSCHAT_STATUS_LINES 32

typedef struct s_syschat t_syschat;

/*
** One connection = one server. Slots live in syschat->conns[] and are
** STABLE: closing a server sets fd = -1 and keeps the slot, so the
** `server_id` stored in every buffer never ends up pointing at another
** server. A late line from a dead connection cannot resurrect an orphan
** tab: buffers_new_on() refuses a slot whose hostname is NULL or whose
** alive flag is cleared.
*/
typedef struct s_conn
{
	int			fd;				/* -1: the slot is free */
	int			alive;
	char		*label;			/* name used by /server and srv/#chan */
	char		*hostname;
	char		*nickname;
	int			port;
	/* TLS is per connection: two servers may disagree on encryption. */
	int			tls;
	int			tls_active;
	int			tls_insecure;
	int			tls_required;
	void		*tls_state;
	/* /list state is per connection: A can list while B stays quiet. */
	int			listing;
	time_t		listing_since;
	int			list_received;
	int			elist_mask;
	char		list_filter[SYSCHAT_NAME_LEN];
	/*
	** Partial line accumulator. It MUST live here: shared between sockets,
	** half a line from A and the start of a line from B would be glued
	** into one plausible but wrong IRC line and nothing would report it.
	*/
	char		net[SYSCHAT_NET_BUFFER];
	int			net_len;
	/*
	** Why a TLS handshake failed, kept so the caller can report it once the
	** window for this connection exists. Empty when TLS was fine.
	*/
	char		tls_reason[256];
	t_syschat	*owner;
} t_conn;

enum
{
	BUF_SERVER = 0,		/* server console window */
	BUF_CHANNEL,		/* #channel */
	BUF_PRIVATE,		/* nickname */
	BUF_LIST			/* the /list window of one server */
};

/*
** One buffer = one tab. `name` is either "#channel", a nickname (private
** message) or the server hostname (the server window of that connection).
** Two servers can both have "#chan": the key is (server_id, name).
** `label` is the addressing form used by /switch and /close -- the bare name
** when it is unique, "srv/chan" when it is not. The buffer bar prints `name`
** only, because it is scoped to the current server and cannot collide.
** `lines` is a ring buffer of already coloured lines; nothing is ever
** dropped unless the ring wraps (SYSCHAT_HISTORY lines of scrollback).
*/
typedef struct s_buffer
{
	char	*name;
	char	label[SYSCHAT_NAME_LEN * 2 + 2];
	unsigned server_id;
	int		kind;
	char	*lines[SYSCHAT_HISTORY];
	int		head;
	int		count;
	int		unread;
	int		joined;
} t_buffer;

typedef struct s_syschat
{
	t_conn				conns[SYSCHAT_MAX_SERVERS];
	int					conn_count;		/* slots never move: high water mark */
	int					current;		/* server of the active buffer */
	/*
	** Before the first connection there is no buffer to write into. What a
	** command says then (usage errors, "?" help) goes into this ring, and
	** screen_draw() paints it in the empty frame -- writing on stdout from
	** a command would be erased by the repaint that follows every command.
	*/
	char				*status[SYSCHAT_STATUS_LINES];
	int					status_count;
	t_buffer			buffers[SYSCHAT_MAX_BUFFERS];
	int					buffer_count;
	int					active;
	struct sockaddr_in	v4_socket;
	struct sockaddr_in6	v6_socket;
	int					running;
} t_syschat;

/* The connection the user is currently looking at. */
#define SYSC(s) (&(s)->conns[(s)->current])

void	syschat_apply_tls_policy(t_conn *conn);
int		syschat_conn_add(t_syschat *syschat, const char *label, const char *host,
			const char *nickname, int port);
void	syschat_conn_close(t_syschat *syschat, int id);
const char	*syschat_conn_name(const t_conn *conn);
void	syschat_say_hello(t_conn *conn);
void	syschat_status(t_syschat *syschat, const char *text);
void	syschat_echo(char *text);
void	syschat_handle_input(char *buffer, int size);
void	syschat_read_network(t_conn *conn);
void	syschat_loop(void);

/*
** Raw mode is entered before the connection is attempted, so a fatal error
** during connect must be able to put the terminal back: without this the
** shell is left with ECHO off and the user types blind.
*/
void	syschat_terminal_raw(void);
void	syschat_terminal_restore(void);

#endif
