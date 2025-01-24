#ifndef ERROR_H
# define ERROR_H

#include "../syschat.h"

void error_exit(t_syschat *syschat, int err);
void error_print(int err);
void error_cleaning(t_syschat *syschat);

#endif
