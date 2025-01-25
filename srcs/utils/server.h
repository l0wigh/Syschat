#ifndef SERVER_H
# define SERVER_H

#include "../syschat.h"

void server_handle_message(t_syschat *syschat, char *message);
char **server_parse_message(char *message);

#endif
