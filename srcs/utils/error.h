#ifndef ERROR_H
# define ERROR_H

#include "../syschat.h"

void error_exit(t_syschat *syschat, int err);
void error_print(int err);
void error_cleaning(t_syschat *syschat);
/*
** --tls-required turned a handshake failure into a hard error: the reason
** goes to stderr (buffer 0 dies with the process) and the exit code is 10.
*/
void error_tls_exit(t_syschat *syschat, const char *reason);

#endif
