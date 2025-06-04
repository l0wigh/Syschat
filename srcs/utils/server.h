#ifndef SERVER_H
# define SERVER_H

#include "../syschat.h"
#include "ctcp.h"

void server_handle_message(t_syschat *syschat, char *message);
char **server_parse_message(char *srv_message);

#endif
