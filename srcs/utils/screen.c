#include "screen.h"
#include "buffers.h"

#define TAB_ACTIVE "\033[7;33m"
#define TAB_UNREAD "\033[1;33m"
#define TAB_IDLE "\033[0;37m"
#define RESET "\033[0m"

static int	screen_width(void)
{
	struct winsize	ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return (ws.ws_col);
	return (80);
}

static int	screen_height(void)
{
	struct winsize	ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 3)
		return (ws.ws_row);
	return (24);
}

/*
** Copy at most `width` *visible* characters, keeping ANSI sequences intact.
** Prevents a long line from wrapping and destroying the layout.
*/
static void	append_clipped(char *dst, size_t dstsz, const char *src, int width)
{
	size_t	used;
	int		visible;
	used = 0;
	visible = 0;
	while (*src && visible < width && used + 2 < dstsz)
	{
		if (*src == '\033')
		{
			while (*src && used + 2 < dstsz)
			{
				dst[used++] = *src;
				if (*src++ == 'm')
					break ;
			}
			continue ;
		}
		dst[used++] = *src++;
		visible++;
	}
	dst[used] = '\0';
}

static size_t	tab_bar(t_syschat *syschat, char *dst, size_t dstsz, int width)
{
	size_t	used;
	char	chunk[128];
	int		len;
	int		visible;

	used = 0;
	visible = 0;
	if (dstsz > 1)
		dst[0] = '\0';
	for (int i = 0; i != syschat->buffer_count; i++)
	{
		len = snprintf(chunk, sizeof(chunk), " %d:%s%s ", i + 1,
				syschat->buffers[i].name,
				syschat->buffers[i].unread ? "*" : "");
		if (len < 0 || visible + len > width)
			break ;
		visible += len;
		used += snprintf(dst + used, dstsz - used, "%s%s",
				i == syschat->active ? TAB_ACTIVE
				: (syschat->buffers[i].unread ? TAB_UNREAD : TAB_IDLE),
				chunk);
		if (used >= dstsz - 1)
			return (dstsz - 1);
	}
	used += snprintf(dst + used, dstsz - used, "%s", RESET);
	return (used);
}

void	screen_setup(t_syschat *syschat)
{
	(void) syschat;
	write(1, "\033[?25h\033[2J\033[H", 13);
}

void	screen_message(const char *text)
{
	write(1, "\033[K", 3);
	if (text)
		write(1, text, strlen(text));
}

void	screen_draw(t_syschat *syschat, const char *input)
{
	char	*out;
	char	clip[SYSCHAT_LINE_LEN + 64];
	size_t	size;
	size_t	used;
	int		rows;
	int		cols;
	int		room;
	int		start;
	int		index;
	t_buffer	*buffer;

	rows = screen_height();
	cols = screen_width();
	size = (size_t)(cols + 128) * (size_t)(rows + 4) + 1024;
	out = calloc(size, sizeof(char));
	if (!out)
		return ;
	used = 0;
	used += snprintf(out + used, size - used, "\033[H");
	used += tab_bar(syschat, out + used, size - used, cols);
	used += snprintf(out + used, size - used, "\033[K\r\n");
	room = rows - 2;
	index = syschat->active;
	if (index >= 0 && index < syschat->buffer_count)
	{
		buffer = &syschat->buffers[index];
		start = buffer->count > room ? buffer->count - room : 0;
		for (int i = start; i != buffer->count; i++)
		{
			clip[0] = '\0';
			append_clipped(clip, sizeof(clip),
				buffer->lines[(buffer->head - buffer->count + i
					+ SYSCHAT_HISTORY * 2) % SYSCHAT_HISTORY], cols - 1);
			used += snprintf(out + used, size - used, "%s\033[K\r\n", clip);
		}
		for (int i = buffer->count; i < room; i++)
			used += snprintf(out + used, size - used, "\033[K\r\n");
	}
	else
		for (int i = 0; i != room; i++)
			used += snprintf(out + used, size - used, "\033[K\r\n");
	if (syschat->active >= 0 && syschat->active < syschat->buffer_count)
		used += snprintf(out + used, size - used, "\033[K%s%s > %s\033[K",
				TAB_ACTIVE, syschat->buffers[syschat->active].name, RESET);
	else
		used += snprintf(out + used, size - used, "\033[K> ");
	append_clipped(clip, sizeof(clip), input ? input : "", cols - 2);
	used += snprintf(out + used, size - used, "%s\033[K", clip);
	write(1, out, used);
	free(out);
}
