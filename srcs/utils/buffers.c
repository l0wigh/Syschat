#include "buffers.h"

/*
** Buffers are matched case-insensitively, like IRC channel names.
*/
int buffers_find(t_syschat *syschat, const char *name)
{
	if (!syschat || !name)
		return (-1);
	for (int i = 0; i != syschat->buffer_count; i++)
		if (syschat->buffers[i].name && strcasecmp(syschat->buffers[i].name, name) == 0)
			return (i);
	return (-1);
}

int buffers_new(t_syschat *syschat, const char *name)
{
	t_buffer	*buffer;

	if (!syschat || !name || name[0] == '\0')
		return (-1);
	if (syschat->buffer_count >= SYSCHAT_MAX_BUFFERS)
		return (-1);
	buffer = &syschat->buffers[syschat->buffer_count];
	bzero(buffer, sizeof(t_buffer));
	buffer->name = strdup(name);
	if (!buffer->name)
		return (-1);
	buffer->head = 0;
	buffer->count = 0;
	buffer->unread = 0;
	buffer->joined = 0;
	syschat->buffer_count++;
	if (syschat->active < 0)
		syschat->active = syschat->buffer_count - 1;
	return (syschat->buffer_count - 1);
}

int buffers_join(t_syschat *syschat, const char *name)
{
	int	index;

	index = buffers_find(syschat, name);
	if (index == -1)
		index = buffers_new(syschat, name);
	if (index == -1)
		return (-1);
	syschat->buffers[index].joined = 1;
	return (index);
}

int buffers_leave(t_syschat *syschat, const char *name)
{
	t_buffer	*buffer;
	int			index;
	int			i;

	index = buffers_find(syschat, name);
	if (index == -1)
		return (-1);
	buffer = &syschat->buffers[index];
	if (buffer->name)
		free(buffer->name);
	for (i = 0; i != SYSCHAT_HISTORY; i++)
		if (buffer->lines[i])
			free(buffer->lines[i]);
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
	return (0);
}

/*
** Never lose an incoming message: an unknown target gets a buffer created
** on the fly (e.g. a private message from someone we never talked to).
*/
int buffers_route(t_syschat *syschat, const char *name)
{
	int	index;

	if (!name || name[0] == '\0')
		return (syschat->active);
	index = buffers_find(syschat, name);
	if (index == -1)
		index = buffers_new(syschat, name);
	return (index);
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
	buffer->lines[slot] = strdup(line);
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
	syschat->buffers[index].unread = 0;
}

void buffers_cycle(t_syschat *syschat, int step)
{
	int	index;

	if (!syschat || syschat->buffer_count <= 0)
		return ;
	index = syschat->active;
	if (index < 0)
		index = 0;
	else
		index = (index + step + syschat->buffer_count) % syschat->buffer_count;
	buffers_switch(syschat, index);
}

char *buffers_active_name(t_syschat *syschat)
{
	if (!syschat || syschat->active < 0 || syschat->active >= syschat->buffer_count)
		return (NULL);
	return (syschat->buffers[syschat->active].name);
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
		if (syschat->buffers[i].name)
			free(syschat->buffers[i].name);
		syschat->buffers[i].name = NULL;
		for (int j = 0; j != SYSCHAT_HISTORY; j++)
		{
			if (syschat->buffers[i].lines[j])
				free(syschat->buffers[i].lines[j]);
			syschat->buffers[i].lines[j] = NULL;
		}
	}
	syschat->buffer_count = 0;
	syschat->active = -1;
}
