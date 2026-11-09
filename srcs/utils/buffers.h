#ifndef BUFFERS_H
# define BUFFERS_H

#include "../syschat.h"

const char *nick_color(const char *nick);

/*
** Buffers are keyed by (server_id, name): two servers can both have a
** "#chan" and they must not merge into one tab. The *_on() form is the
** explicit one; the historic form (buffers_find & co) applies to the
** server of the active buffer, which is what every user command means.
*/
int		buffers_find_on(t_syschat *syschat, unsigned server_id, const char *name);
int		buffers_new_on(t_syschat *syschat, unsigned server_id, const char *name);
int		buffers_join_on(t_syschat *syschat, unsigned server_id, const char *name);
int		buffers_leave_on(t_syschat *syschat, unsigned server_id, const char *name);
int		buffers_route_on(t_syschat *syschat, unsigned server_id, const char *name);

int		buffers_find(t_syschat *syschat, const char *name);
int		buffers_new(t_syschat *syschat, const char *name);
int		buffers_join(t_syschat *syschat, const char *name);
int		buffers_leave(t_syschat *syschat, const char *name);
int		buffers_route(t_syschat *syschat, const char *name);

/*
** Drops every buffer of one server in a single pass. Looping on
** buffers_leave() would shift the table N times and invalidate the
** intermediate `active` indexes.
*/
void	buffers_leave_server(t_syschat *syschat, unsigned server_id);
/* Rebuilds the tab labels ("#chan" or "srv/#chan") after a change. */
void	buffers_relabel(t_syschat *syschat);

void	buffers_append(t_syschat *syschat, int index, const char *line);
void	buffers_clear(t_syschat *syschat, int index);
void	buffers_switch(t_syschat *syschat, int index);
void	buffers_cycle(t_syschat *syschat, int step);
char	*buffers_active_name(t_syschat *syschat);
int		buffers_active_kind(t_syschat *syschat);
int		buffers_unread_total(t_syschat *syschat);
void	buffers_free(t_syschat *syschat);

#endif
