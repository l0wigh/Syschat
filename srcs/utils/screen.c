#include "screen.h"
#include "buffers.h"

#define TAB_ACTIVE "\033[1;36m"
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
** Bytes making up the UTF-8 character starting at `s` (at least 1).
** A byte is a continuation byte when its top two bits are 10.
*/
static size_t	utf8_len(const char *s)
{
	unsigned char	c;

	c = (unsigned char)*s;
	if ((c & 0xE0) == 0xC0)
		return (2);
	if ((c & 0xF0) == 0xE0)
		return (3);
	if ((c & 0xF8) == 0xF0)
		return (4);
	return (1);
}

/* Displayed columns of an ASCII/UTF-8 string (one column per character). */
int	screen_text_width(const char *s)
{
	int	width;

	width = 0;
	while (*s)
	{
		if (((unsigned char)*s & 0xC0) != 0x80)
			width++;
		s++;
	}
	return (width);
}

/*
** Copy at most `width` *visible* characters, keeping ANSI sequences intact.
** Counts characters, not bytes: one 'é' is one column, and a multi-byte
** character is copied whole or not at all, never split in half.
*/
static void	append_clipped(char *dst, size_t dstsz, const char *src, int width)
{
	size_t	used;
	size_t	len;
	int		visible;

	used = 0;
	visible = 0;
	while (*src && visible < width)
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
		len = utf8_len(src);
		if (used + len + 1 >= dstsz)
			break ;
		memcpy(dst + used, src, len);
		used += len;
		src += len;
		visible++;
	}
	dst[used] = '\0';
}

/*
** Both bars are the same widget: a list of chips where the active one must
** stay visible, so the bar starts as late as possible instead of truncating
** from the left. The right segment reserves its columns first, or it would
** never be drawn on a loaded client.
*/
typedef struct s_chip
{
	const char	*label;
	int			unread;
	int			active;
} t_chip;

static size_t	chips_bar(t_chip *chips, int n, const char *right_text,
	char *dst, size_t dstsz, int width)
{
	size_t	used;
	int		widths[SYSCHAT_MAX_BUFFERS];
	int		right_len;
	int		avail;
	int		visible;
	int		start;
	int		active;
	int		i;

	used = 0;
	visible = 0;
	if (dstsz > 1)
		dst[0] = '\0';
	active = -1;
	right_len = 0;
	if (right_text && width >= 40)
	{
		right_len = screen_text_width(right_text);
		if (right_len < 0 || right_len > width / 2)
			right_len = 0;
	}
	avail = width - (right_len ? right_len + 2 : 0);
	if (avail < 8)
		avail = 8;
	for (i = 0; i != n; i++)
	{
		/* every chip is " name ", plus the unread marker when there is one */
		widths[i] = screen_text_width(chips[i].label) + 2
			+ (chips[i].unread ? 1 : 0);
		if (chips[i].active)
			active = i;
	}
	/* Not even the active chip fits: keep a position marker, never a blank
	** bar the user cannot read anything from. */
	if (active >= 0 && widths[active] > avail)
	{
		used += snprintf(dst + used, dstsz - used, "%s%d/%d%s%s", TAB_ACTIVE,
			active + 1, n, chips[active].unread ? "*" : "", RESET);
		return (used);
	}
	start = 0;
	if (active >= 0)
	{
		int	room = avail;

		for (i = active; i >= 0; i--)
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
	for (i = start; i != n; i++)
	{
		if (visible + widths[i] > avail)
			break ;
		visible += widths[i];
		if (i == active)
			used += snprintf(dst + used, dstsz - used, "%s %s %s",
				TAB_ACTIVE, chips[i].label, RESET);
		else
			used += snprintf(dst + used, dstsz - used, "%s %s%s ",
				chips[i].unread ? TAB_UNREAD : TAB_IDLE,
				chips[i].label,
				chips[i].unread ? "*" : "");
		if (used >= dstsz - 1)
			return (dstsz - 1);
	}
	if (i != n && visible < avail)
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
		used += snprintf(dst + used, dstsz - used, "\e[90m%s", right_text);
		used += snprintf(dst + used, dstsz - used, "%s", RESET);
	}
	return (used);
}

/*
** Top bar: the servers. One chip per live connection, the current one
** highlighted. A closed slot is not a chip: it would advertise a server the
** user can no longer reach.
*/
static int		server_chips(t_syschat *syschat, t_chip *chips)
{
	int	n;

	n = 0;
	for (int i = 0; i != syschat->conn_count && n < SYSCHAT_MAX_SERVERS; i++)
	{
		if (!syschat->conns[i].alive)
			continue ;
		chips[n].label = syschat->conns[i].label ? syschat->conns[i].label : "?";
		chips[n].unread = 0;
		chips[n].active = (i == syschat->current);
		n++;
	}
	return (n);
}

/*
** Bottom bar: the buffers of the CURRENT server only -- its channels, its
** private messages and its /list window. The server console is not here, it
** is the chip of the top bar. They are printed by `name`, not `label`: the
** scope is one server, so a homonymous "#chan" on another server cannot
** collide in this bar, and the "srv/#chan" form stays a command syntax.
*/
static int		buffer_chips(t_syschat *syschat, t_chip *chips)
{
	int	n;

	n = 0;
	for (int i = 0; i != syschat->buffer_count && n < SYSCHAT_MAX_BUFFERS; i++)
	{
		t_buffer	*b = &syschat->buffers[i];

		if (b->kind == BUF_SERVER || b->server_id != (unsigned) syschat->current)
			continue ;
		chips[n].label = b->name;
		chips[n].unread = b->unread;
		chips[n].active = (i == syschat->active);
		n++;
	}
	return (n);
}

static void	row_push(char ***rows, int *count, char *row)
{
	char	**grown;

	grown = realloc(*rows, sizeof(char *) * (*count + 1));
	if (!grown)
	{
		free(row);
		return ;
	}
	*rows = grown;
	grown[*count] = row;
	(*count)++;
}

/*
** Wrap one buffered line into rows of at most `width` visible columns.
** ANSI sequences are copied through and the last colour seen is re-emitted at
** the start of each continuation row, so a wrapped coloured message keeps its
** colour. A multi-byte character is never split. Returns the row count.
*/
static int	wrap_line(const char *src, int width, char ***out)
{
	char	**rows;
	char	*row;
	char	sgr[24];
	size_t	cap;
	size_t	len;
	size_t	start;
	size_t	step;
	int		visible;
	int		limit;
	int		count;

	*out = NULL;
	if (width < 1)
		width = 1;
	cap = (size_t)width * 4 + 64;
	rows = NULL;
	count = 0;
	row = calloc(cap, sizeof(char));
	if (!row)
		return (0);
	len = 0;
	visible = 0;
	limit = width;
	sgr[0] = '\0';
	while (*src)
	{
		if (*src == '\033')
		{
			start = len;
			while (*src && len + 1 < cap)
			{
				row[len++] = *src;
				if (*src++ == 'm')
					break ;
			}
			row[len] = '\0';
			if (len > start && len - start < sizeof(sgr))
				memcpy(sgr, row + start, len - start + 1);
			continue ;
		}
		if (visible == limit)
		{
			row[len] = '\0';
			row_push(&rows, &count, row);
			row = calloc(cap, sizeof(char));
			if (!row)
				break ;
			len = 0;
			visible = 0;
			/* Continuation rows are indented with spaces (never a bar: a
			** copy from the terminal must stay clean) and lose the same
			** width, so no row can overflow the terminal. */
			limit = width > 3 ? width - 2 : width;
			if (sgr[0])
			{
				memcpy(row, sgr, strlen(sgr));
				len = strlen(sgr);
			}
			if (limit != width && len + 3 < cap)
			{
				row[len++] = ' ';
				row[len++] = ' ';
			}
			while (*src == ' ')
				src++;
			continue ;
		}
		step = utf8_len(src);
		if (len + step + 1 >= cap)
			break ;
		memcpy(row + len, src, step);
		len += step;
		src += step;
		visible++;
	}
	row[len] = '\0';
	if (len || count == 0)
		row_push(&rows, &count, row);
	else
		free(row);
	*out = rows;
	return (count);
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
	char	**rows;
	int		nrows;
	int		top_cut;
	int		rows_count;
	int		cols;
	int		room;
	int			index;
	int			nchips;
	int			buffer_rows;
	char		right[32];
	t_chip		chips[SYSCHAT_MAX_BUFFERS];
	t_buffer	*buffer;

	rows_count = screen_height();
	cols = screen_width();
	size = (size_t)(cols + 128) * (size_t)(rows_count + 4) + 1024;
	out = calloc(size, sizeof(char));
	if (!out)
		return ;
	used = 0;
	used += snprintf(out + used, size - used, "\033[H");
	/*
	** Two bars: servers on top, then the buffers of the current server. The
	** bottom bar is skipped when the current server has none (its console is
	** the only tab), so the conversation keeps that row.
	*/
	nchips = server_chips(syschat, chips);
	if (nchips)
	{
		snprintf(right, sizeof(right), "%d/%d srv", nchips, SYSCHAT_MAX_SERVERS);
		used += chips_bar(chips, nchips, right, out + used, size - used, cols);
	}
	else
		used += snprintf(out + used, size - used, "%s-- no server --%s",
			TAB_IDLE, RESET);
	used += snprintf(out + used, size - used, "\033[K\r\n");
	buffer_rows = 0;
	nchips = buffer_chips(syschat, chips);
	if (nchips)
	{
		snprintf(right, sizeof(right), "%d buf%s", nchips, nchips > 1 ? "s" : "");
		used += chips_bar(chips, nchips, right, out + used, size - used, cols);
		used += snprintf(out + used, size - used, "\033[K\r\n");
		buffer_rows = 1;
	}
	room = rows_count - 2 - buffer_rows;
	if (room < 0)
		room = 0;
	index = syschat->active;
	if (index >= 0 && index < syschat->buffer_count && room > 0)
	{
		buffer = &syschat->buffers[index];
		/* Long lines wrap instead of being cut at the right edge. Rows are
		** collected from the newest line backwards so the view stays anchored
		** on the last message, and only the last `room` rows are kept. */
		rows = NULL;
		nrows = 0;
		top_cut = 0;
		for (int i = buffer->count - 1; i >= 0 && nrows < room; i--)
		{
			char	**wrapped;
			int		count;

			count = wrap_line(buffer->lines[(buffer->head - buffer->count + i
						+ SYSCHAT_HISTORY * 2) % SYSCHAT_HISTORY], cols - 1,
					&wrapped);
			for (int j = count - 1; j >= 0; j--)
			{
				if (nrows < room)
					row_push(&rows, &nrows, wrapped[j]);
				else
				{
					/* This line starts above the view: say so instead of
					** letting the top row begin in the middle of a sentence. */
					top_cut = 1;
					free(wrapped[j]);
				}
			}
			free(wrapped);
		}
		for (int i = nrows - 1; i >= 0; i--)
		{
			/* The marker replaces the continuation indent, so the row keeps
			** exactly the width it was wrapped for. */
			if (top_cut && i == nrows - 1)
				used += snprintf(out + used, size - used,
						"\033[90m..\033[0m%s\033[K\r\n",
						rows[i] + (strncmp(rows[i], "  ", 2) == 0 ? 2 : 0));
			else
				used += snprintf(out + used, size - used, "%s\033[K\r\n", rows[i]);
			free(rows[i]);
		}
		free(rows);
		for (int i = nrows; i < room; i++)
			used += snprintf(out + used, size - used, "\033[K\r\n");
	}
	else if (syschat->buffer_count == 0)
	{
		/* Started with no argument: say what to do, then whatever the last
		** commands answered. They cannot go into a buffer (there is none). */
		int	line;

		used += snprintf(out + used, size - used,
			"\033[0;37mNo server yet. /connect <name> <host> [port] to open one, "
			"/? for help.\033[0m\033[K\r\n");
		line = 1;
		/* The ring is right aligned: the oldest entry sits at
		** SYSCHAT_STATUS_LINES - status_count, not at 0. */
		for (int i = SYSCHAT_STATUS_LINES - syschat->status_count;
			i != SYSCHAT_STATUS_LINES && line < room; i++, line++)
			used += snprintf(out + used, size - used, "%s\033[K\r\n",
				syschat->status[i] ? syschat->status[i] : "");
		for (; line < room; line++)
			used += snprintf(out + used, size - used, "\033[K\r\n");
	}
	else
		for (int i = 0; i != room; i++)
			used += snprintf(out + used, size - used, "\033[K\r\n");
	if (syschat->active >= 0 && syschat->active < syschat->buffer_count)
	{
		const char	*name = syschat->buffers[syschat->active].name;
		const char	*nick = SYSC(syschat)->nickname ? SYSC(syschat)->nickname : "";
		/* "[#chan] " is strlen+3 visible, "<nick> " is strlen+4. */
		int			plen = (int)(strlen(name) + strlen(nick)) + 7;

		used += snprintf(out + used, size - used,
				"\033[K\033[90m[%s]\033[0m \033[1;36m<%s>\033[0m ", name, nick);
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
