#include "screen.h"
#include "buffers.h"

#define TAB_ACTIVE "\033[7;33m"
#define TAB_UNREAD "\033[1;33m"
#define TAB_IDLE "\033[0;37m"
#define RESET "\033[0m"

int	screen_width(void)
{
	struct winsize	ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return (ws.ws_col);
	return (80);
}

int	screen_height(void)
{
	struct winsize	ws;

	/* Down to 2 rows the layout still holds (tab bar + prompt); below that
	** there is nothing to lay out, and claiming 24 would make the client
	** scroll its own screen on every redraw. */
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_row == 0)
		return (24);
	return (ws.ws_row < 2 ? 2 : ws.ws_row);
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

/*
** Tab bar: the active buffer must stay visible, so the bar scrolls instead of
** truncating from the left. The right segment reserves its columns first, or
** it would never be drawn on a loaded client.
*/
static size_t	tab_bar(t_syschat *syschat, char *dst, size_t dstsz, int width)
{
	size_t	used;
	char	right[SYSCHAT_NAME_LEN + 24];
	int		widths[SYSCHAT_MAX_BUFFERS];
	int		right_len;
	int		avail;
	int		visible;
	int		start;
	int		i;

	used = 0;
	visible = 0;
	if (dstsz > 1)
		dst[0] = '\0';
	right_len = 0;
	right[0] = '\0';
	if (width >= 40)
	{
		right_len = snprintf(right, sizeof(right), "%d buf%s",
			syschat->buffer_count, syschat->buffer_count > 1 ? "s" : "");
		if (right_len < 0 || right_len > width / 2)
			right_len = 0;
	}
	avail = width - (right_len ? right_len + 2 : 0);
	if (avail < 8)
		avail = 8;
	for (i = 0; i != syschat->buffer_count; i++)
	{
		/* active: " name " (no brackets), others: " [name]". */
		widths[i] = (int)strlen(syschat->buffers[i].name) + 2
			+ (i == syschat->active ? 0 : 1)
			+ (syschat->buffers[i].unread ? 1 : 0);
	}
	/* Not even the active tab fits: keep a position marker, never a blank
	** bar the user cannot read anything from. */
	if (syschat->active >= 0 && syschat->active < syschat->buffer_count
		&& widths[syschat->active] > avail)
	{
		used += snprintf(dst + used, dstsz - used, "%s%d/%d%s%s", TAB_ACTIVE,
			syschat->active + 1, syschat->buffer_count,
			syschat->buffers[syschat->active].unread ? "*" : "", RESET);
		return (used);
	}
	start = 0;
	if (syschat->active >= 0 && syschat->active < syschat->buffer_count)
	{
		int	room = avail;

		for (i = syschat->active; i >= 0; i--)
		{
			room -= widths[i];
			if (room < 0)
				break ;
			start = i;
		}
	}
	if (start > 0)
	{
		used += snprintf(dst + used, dstsz - used, "%s<", TAB_IDLE);
		visible++;
	}
	for (i = start; i != syschat->buffer_count; i++)
	{
		if (visible + widths[i] > avail)
			break ;
		visible += widths[i];
		if (i == syschat->active)
			used += snprintf(dst + used, dstsz - used, "%s %s %s",
				TAB_ACTIVE, syschat->buffers[i].name, RESET);
		else
			used += snprintf(dst + used, dstsz - used, "%s [%s%s]%s",
				syschat->buffers[i].unread ? TAB_UNREAD : TAB_IDLE,
				syschat->buffers[i].name,
				syschat->buffers[i].unread ? "*" : "", RESET);
		if (used >= dstsz - 1)
			return (dstsz - 1);
	}
	if (i != syschat->buffer_count && visible < avail)
	{
		used += snprintf(dst + used, dstsz - used, "%s>", TAB_IDLE);
		visible++;
	}
	used += snprintf(dst + used, dstsz - used, "%s", RESET);
	if (right_len)
	{
		while (visible < width - right_len && used + 1 < dstsz)
		{
			dst[used++] = ' ';
			visible++;
		}
		dst[used] = '\0';
		used += snprintf(dst + used, dstsz - used, "\e[90m%s", right);
		used += snprintf(dst + used, dstsz - used, "%s", RESET);
	}
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
	if (room < 0)
		room = 0;
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
	{
		const char	*name = syschat->buffers[syschat->active].name;
		const char	*nick = syschat->nickname ? syschat->nickname : "";
		/* "[#chan] " is strlen+3 visible, "<nick> " is strlen+4. */
		int			plen = (int)(strlen(name) + strlen(nick)) + 7;

		used += snprintf(out + used, size - used,
				"\033[K\033[90m[%s]\033[0m \033[1;37m<%s>\033[0m ", name, nick);
		append_clipped(clip, sizeof(clip), input ? input : "", cols - plen);
	}
	else
	{
		used += snprintf(out + used, size - used, "\033[K> ");
		append_clipped(clip, sizeof(clip), input ? input : "", cols - 2);
	}
	used += snprintf(out + used, size - used, "%s\033[K", clip);
	write(1, out, used);
	free(out);
}
