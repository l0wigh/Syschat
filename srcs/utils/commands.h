#ifndef COMMANDS_H
# define COMMANDS_H

#include "../syschat.h"
#include "error.h"

const char *send_privmsg(t_syschat *syschat, const char *target, const char *text, int *truncated);
void commands_execute(t_syschat *syschat, char *command);
char **commands_parse(char *command);

#endif
