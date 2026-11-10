#include "buffers.h"
#include <time.h>

/*
** Buffers are matched case-insensitively, like IRC channel names, and the
** key is (server_id, name) so two servers can both hold "#chan".
*/
/*
** Five period-accurate accents, no blue: the body text stays default so a
** channel full of blue is impossible by construction.
*/
static const char *NICK_COLORS[] = {
	"\e[0;33m",	/* yellow */
	"\e[0;32m",	/* green */
	"\e[0;35m",	/* magenta */
	"\e[0;36m",	/* cyan */
	"\e[1;31m",	/* light red */
	"\e[1;32m",	/* light green */
	"\e[1;35m"	/* light magenta */
};
#define NICK_COLOR_COUNT 7

const char *nick_color(const char *nick)
{
	unsigned int	hash;
	const char		*p;

	if (!nick || !nick[0])
		return (NICK_COLORS[0]);
	p = nick;
	/* Status prefix is not part of the identity: @bob and bob are the same. */
	while (*p && strchr("@+%~&", *p))
		p++;
	hash = 0;
	while (*p)
	{
		hash = hash * 31 + (unsigned char) *p;
		p++;
	}
	return (NICK_COLORS[hash % NICK_COLOR_COUNT]);
}

static const char *server_label(t_syschat *syschat, unsigned server_id)
{
	if (server_id < (unsigned) syschat->conn_count && syschat->conns[server_id].label)
		return (syschat->conns[server_id].label);
	return ("?");
}

static int buffer_kind_of(t_syschat *syschat, unsigned server_id, const char *name)
{
	if (!strcasecmp(name, server_label(syschat, server_id)))
		return (BUF_SERVER);
	if (name[0] == '#')
		return (BUF_CHANNEL);
	if (!strcasecmp(name, SYSCHAT_LIST_BUFFER))
		return (BUF_LIST);
	return (BUF_PRIVATE);
}

int buffers_find_on(t_syschat *syschat, unsigned server_id, const char *name)
{
	if (!syschat || !name)
		return (-1);
	for (int i = 0; i != syschat->buffer_count; i++)
		if (syschat->buffers[i].server_id == server_id && syschat->buffers[i].name
			&& strcasecmp(syschat->buffers[i].name, name) == 0)
			return (i);
	return (-1);
}

int buffers_find(t_syschat *syschat, const char *name)
{
	if (!syschat)
		return (-1);
	return (buffers_find_on(syschat, (unsigned) syschat->current, name));
}

int buffers_new_on(t_syschat *syschat, unsigned server_id, const char *name)
{
	t_buffer	*buffer;

	if (!syschat || !name || name[0] == '\0')
		return (-1);
	/* A buffer can never belong to a server slot that does not exist: a
	** late PART after /disconnect must not resurrect an orphan tab. */
	if (server_id >= (unsigned) syschat->conn_count)
		return (-1);
	if (syschat->conn_count > (int) server_id && !syschat->conns[server_id].alive
		&& syschat->conns[server_id].hostname)
		return (-1);
	if (syschat->buffer_count >= SYSCHAT_MAX_BUFFERS)
		return (-1);
	buffer = &syschat->buffers[syschat->buffer_count];
	bzero(buffer, sizeof(t_buffer));
	buffer->name = strdup(name);
	if (!buffer->name)
		return (-1);
	buffer->server_id = server_id;
	buffer->kind = buffer_kind_of(syschat, server_id, name);
	buffer->head = 0;
	buffer->count = 0;
	buffer->unread = 0;
	buffer->joined = 0;
	syschat->buffer_count++;
	if (syschat->active < 0)
		syschat->active = syschat->buffer_count - 1;
	buffers_relabel(syschat);
	return (syschat->buffer_count - 1);
}

int buffers_new(t_syschat *syschat, const char *name)
{
	if (!syschat)
		return (-1);
	return (buffers_new_on(syschat, (unsigned) syschat->current, name));
}

int buffers_join_on(t_syschat *syschat, unsigned server_id, const char *name)
{
	int	index;

	index = buffers_find_on(syschat, server_id, name);
	if (index == -1)
		index = buffers_new_on(syschat, server_id, name);
	if (index == -1)
		return (-1);
	syschat->buffers[index].joined = 1;
	return (index);
}

int buffers_join(t_syschat *syschat, const char *name)
{
	if (!syschat)
		return (-1);
	return (buffers_join_on(syschat, (unsigned) syschat->current, name));
}

/* Frees the payload of one buffer; the slot is left zeroed by the caller. */
static void	buffer_dispose(t_buffer *buffer)
{
	int	i;

	if (buffer->name)
		free(buffer->name);
	buffer->name = NULL;
	for (i = 0; i != SYSCHAT_HISTORY; i++)
		if (buffer->lines[i])
			free(buffer->lines[i]);
}

int buffers_leave_on(t_syschat *syschat, unsigned server_id, const char *name)
{
	t_buffer	*buffer;
	int		index;
	int		i;

	index = buffers_find_on(syschat, server_id, name);
	if (index == -1)
		return (-1);
	buffer = &syschat->buffers[index];
	buffer_dispose(buffer);
	for (i = index; i + 1 < syschat->buffer_count; i++)
		syschat->buffers[i] = syschat->buffers[i + 1];
	syschat->buffer_count--;
	bzero(&syschat->buffers[syschat->buffer_count], sizeof(t_buffer));
	if (syschat->buffer_count == 0)
		syschat->active = -1;
	else if (syschat->active >= syschat->buffer_count)
		syschat->active = syschat->buffer_count - 1;
	else if (syschat->active > index)
		syschat->active--;
	if (syschat->active >= 0)
		syschat->current = syschat->buffers[syschat->active].server_id;
	buffers_relabel(syschat);
	return (0);
}

int buffers_leave(t_syschat *syschat, const char *name)
{
	if (!syschat)
		return (-1);
	return (buffers_leave_on(syschat, (unsigned) syschat->current, name));
}

/*
** One pass, no index renumbering of the survivors: closing a server must
** not shuffle server_id values around (server slots are stable).
*/
void buffers_leave_server(t_syschat *syschat, unsigned server_id)
{
	int	dst;
	int	active;
	int	i;

	if (!syschat)
		return ;
	dst = 0;
	active = -1;
	for (i = 0; i != syschat->buffer_count; i++)
	{
		if (syschat->buffers[i].server_id == server_id)
		{
			buffer_dispose(&syschat->buffers[i]);
			continue ;
		}
		if (i == syschat->active)
			active = dst;
		if (dst != i)
			syschat->buffers[dst] = syschat->buffers[i];
		dst++;
	}
	for (i = dst; i != syschat->buffer_count; i++)
		bzero(&syschat->buffers[i], sizeof(t_buffer));
	syschat->buffer_count = dst;
	if (active >= 0)
		syschat->active = active;
	if (syschat->buffer_count == 0)
		syschat->active = -1;
	else if (syschat->active < 0 || syschat->active >= syschat->buffer_count)
		syschat->active = syschat->buffer_count - 1;
	if (syschat->active >= 0)
		syschat->current = syschat->buffers[syschat->active].server_id;
	buffers_relabel(syschat);
}

/*
** The tab bar cannot print " #chan  #chan " and pretend the user can tell
** them apart: a name that exists on more than one server is prefixed with
** its server label.
*/
void buffers_relabel(t_syschat *syschat)
{
	int	i;
	int	j;
	int	dupe;

	if (!syschat)
		return ;
	for (i = 0; i != syschat->buffer_count; i++)
	{
		dupe = 0;
		for (j = 0; j != syschat->buffer_count && !dupe; j++)
			if (j != i && syschat->buffers[j].server_id != syschat->buffers[i].server_id
				&& syschat->buffers[j].name
				&& !strcasecmp(syschat->buffers[j].name, syschat->buffers[i].name))
				dupe = 1;
		if (dupe)
			snprintf(syschat->buffers[i].label, sizeof(syschat->buffers[i].label),
				"%s/%s", server_label(syschat, syschat->buffers[i].server_id),
				syschat->buffers[i].name);
		else
			snprintf(syschat->buffers[i].label, sizeof(syschat->buffers[i].label),
				"%s", syschat->buffers[i].name);
	}
}

void buffers_notice(t_syschat *syschat, unsigned server_id, const char *text)
{
	int	index;

	if (!syschat || !text)
		return ;
	index = -1;
	if (server_id < (unsigned) syschat->conn_count && syschat->conns[server_id].alive
		&& syschat->conns[server_id].label)
		index = buffers_find_on(syschat, server_id, syschat->conns[server_id].label);
	if (index == -1)
	{
		/*
		** The server this notice is about is already gone (a drop, or
		** /disconnect). Aim at the console of a surviving connection:
		** falling back to `active` would print "B disconnected" inside
		** A's #channel, which is worse than printing nothing.
		*/
		for (int i = 0; i != syschat->conn_count && index == -1; i++)
			if (syschat->conns[i].alive && syschat->conns[i].label)
				index = buffers_find_on(syschat, (unsigned) i, syschat->conns[i].label);
	}
	if (index == -1 && syschat->active >= 0 && syschat->active < syschat->buffer_count)
		index = syschat->active;
	if (index == -1 && syschat->buffer_count > 0)
		index = 0;
	if (index != -1)
		buffers_append(syschat, index, text);
}

/*
** Never lose an incoming message: an unknown target gets a buffer created
** on the fly (e.g. a private message from someone we never talked to).
*/
int buffers_route_on(t_syschat *syschat, unsigned server_id, const char *name)
{
	int	index;

	if (!syschat)
		return (-1);
	if (!name || name[0] == '\0')
		return (syschat->active);
	index = buffers_find_on(syschat, server_id, name);
	if (index == -1)
		index = buffers_new_on(syschat, server_id, name);
	return (index);
}

int buffers_route(t_syschat *syschat, const char *name)
{
	if (!syschat)
		return (-1);
	return (buffers_route_on(syschat, (unsigned) syschat->current, name));
}

/*
** Every line of every buffer goes through here, so the timestamp cannot be
** forgotten at one call site. 8 visible columns: "[HH:MM] ".
*/
static const char *stamped_line(const char *line)
{
	static char	stamped[BF_SIZE * 2 + 32];
	time_t		now;
	struct tm	*tm;

	now = time(NULL);
	tm = localtime(&now);
	if (!tm)
		return (line);
	snprintf(stamped, sizeof(stamped), "\e[90m[%02d:%02d]\e[0m %s", tm->tm_hour, tm->tm_min, line);
	return (stamped);
}

void buffers_append(t_syschat *syschat, int index, const char *line)
{
	t_buffer	*buffer;
	char		*pos;
	int			slot;

	if (!syschat || index < 0 || index >= syschat->buffer_count || !line)
		return ;
	buffer = &syschat->buffers[index];
	slot = buffer->head;
	if (buffer->lines[slot])
		free(buffer->lines[slot]);
	buffer->lines[slot] = strdup(stamped_line(line));
	if (!buffer->lines[slot])
		return ;
	pos = strchr(buffer->lines[slot], '\n');
	if (pos)
		*pos = '\0';
	buffer->head = (buffer->head + 1) % SYSCHAT_HISTORY;
	if (buffer->count < SYSCHAT_HISTORY)
		buffer->count++;
	if (index != syschat->active)
		buffer->unread++;
}

void buffers_clear(t_syschat *syschat, int index)
{
	t_buffer	*buffer;

	if (!syschat || index < 0 || index >= syschat->buffer_count)
		return ;
	buffer = &syschat->buffers[index];
	for (int i = 0; i != SYSCHAT_HISTORY; i++)
	{
		if (buffer->lines[i])
			free(buffer->lines[i]);
		buffer->lines[i] = NULL;
	}
	buffer->head = 0;
	buffer->count = 0;
}

void buffers_switch(t_syschat *syschat, int index)
{
	if (!syschat || index < 0 || index >= syschat->buffer_count)
		return ;
	syschat->active = index;
	syschat->current = syschat->buffers[index].server_id;
	syschat->buffers[index].unread = 0;
}

void buffers_cycle(t_syschat *syschat, int step)
{
	int	list[SYSCHAT_MAX_BUFFERS];
	int	n;
	int	pos;

	if (!syschat || syschat->buffer_count <= 0)
		return ;
	/*
	** Tab / Shift-Tab move inside the buffer bar, which only holds the
	** buffers of the CURRENT server -- its channels, its private messages
	** and its /list window. Jumping to another server's "chan" from a key
	** press is the silent routing the multi-server work forbids; /server and
	** /switch <label>/<buffer> stay the explicit ways across servers.
	*/
	n = 0;
	pos = -1;
	for (int i = 0; i != syschat->buffer_count; i++)
	{
		t_buffer	*b = &syschat->buffers[i];

		if (b->kind == BUF_SERVER || b->server_id != (unsigned) syschat->current)
			continue ;
		if (i == syschat->active)
			pos = n;
		list[n++] = i;
	}
	if (n == 0)
		return ;
	if (pos < 0)
		pos = (step > 0) ? -1 : 0;
	buffers_switch(syschat, list[(pos + step + n) % n]);
}

char *buffers_active_name(t_syschat *syschat)
{
	if (!syschat || syschat->active < 0 || syschat->active >= syschat->buffer_count)
		return (NULL);
	return (syschat->buffers[syschat->active].name);
}

int buffers_active_kind(t_syschat *syschat)
{
	if (!syschat || syschat->active < 0 || syschat->active >= syschat->buffer_count)
		return (-1);
	return (syschat->buffers[syschat->active].kind);
}

int buffers_unread_total(t_syschat *syschat)
{
	int	total = 0;

	for (int i = 0; i != syschat->buffer_count; i++)
		total += syschat->buffers[i].unread;
	return (total);
}

void buffers_free(t_syschat *syschat)
{
	if (!syschat)
		return ;
	for (int i = 0; i != syschat->buffer_count; i++)
	{
		buffer_dispose(&syschat->buffers[i]);
		syschat->buffers[i].name = NULL;
		for (int j = 0; j != SYSCHAT_HISTORY; j++)
			syschat->buffers[i].lines[j] = NULL;
	}
	syschat->buffer_count = 0;
	syschat->active = -1;
}
