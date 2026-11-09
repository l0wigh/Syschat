#ifndef SERVER_H
# define SERVER_H

#include "../syschat.h"
#include "ctcp.h"

int		server_handle_message(t_syschat *syschat, char *message, char *target);
char **server_parse_message(char *srv_message);

#endif
