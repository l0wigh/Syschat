#ifndef COMMANDS_H
# define COMMANDS_H

#include "../syschat.h"
#include "error.h"

void commands_execute(t_syschat *syschat, char *command);
char **commands_parse(char *command);

#endif
