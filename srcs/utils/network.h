#ifndef CONNECT_H
# define CONNECT_H

#include "../syschat.h"
#include "error.h"

void network_prepare_epoll(t_syschat *syschat);
void network_connect_server(t_syschat *syschat);
int network_get_ip(t_syschat *syschat, char *ip);

#endif
