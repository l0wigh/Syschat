#ifndef BUFFERS_H
# define BUFFERS_H

#include "../syschat.h"

const char *nick_color(const char *nick);
int		buffers_find(t_syschat *syschat, const char *name);
int		buffers_new(t_syschat *syschat, const char *name);
int		buffers_join(t_syschat *syschat, const char *name);
int		buffers_leave(t_syschat *syschat, const char *name);
int		buffers_route(t_syschat *syschat, const char *name);
void	buffers_append(t_syschat *syschat, int index, const char *line);
void	buffers_clear(t_syschat *syschat, int index);
void	buffers_switch(t_syschat *syschat, int index);
void	buffers_cycle(t_syschat *syschat, int step);
char	*buffers_active_name(t_syschat *syschat);
int		buffers_unread_total(t_syschat *syschat);
void	buffers_free(t_syschat *syschat);

#endif
