#ifndef SCREEN_H
# define SCREEN_H

#include "../syschat.h"

void	screen_setup(t_syschat *syschat);
void	screen_draw(t_syschat *syschat, const char *input);
void	screen_message(const char *text);

#endif
