#ifndef SERVER_H
# define SERVER_H

#include "../syschat.h"
#include "ctcp.h"

/* `conn` is the connection the line came from: routing must use its
** server id, never "the" current server. */
int		server_handle_message(t_syschat *syschat, t_conn *conn, char *message, char *target);
char **server_parse_message(char *srv_message);

#endif
