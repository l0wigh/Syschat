#ifndef SCREEN_H
# define SCREEN_H

#include "../syschat.h"

int		screen_width(void);
int		screen_height(void);
void	screen_setup(t_syschat *syschat);
void	screen_draw(t_syschat *syschat, const char *input);
void	screen_message(const char *text);

#endif
