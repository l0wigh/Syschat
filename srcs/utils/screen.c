#include "screen.h"
#include "buffers.h"

#include <stdarg.h>

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
** Append a formatted fragment to the frame, clamped: `used` never leaves
** [0, size - 1], so the `size - used` every other write trusts cannot
** underflow. A frame that would not fit is cut at the end instead of
** running past the allocation.
*/
static void	out_add(char *out, size_t size, size_t *used, const char *fmt, ...)
{
	va_list	ap;
	int		written;

	if (*used >= size - 1)
		return ;
	va_start(ap, fmt);
	written = vsnprintf(out + *used, size - *used, fmt, ap);
	va_end(ap);
	if (written < 0)
		return ;
	*used += (size_t)written;
	if (*used > size - 1)
		*used = size - 1;
}

/*
** A wrapped continuation row can start with the colour of the prefix (a
** reset, in practice) before its two-space indent. Callers that replace the
** indent -- the ".." marker of a truncated view -- must skip both.
*/
static const char	*continuation_body(const char *row)
{
	while (*row == '\033')
	{
		while (*row && *row != 'm')
			row++;
		if (*row)
			row++;
	}
	if (row[0] == ' ' && row[1] == ' ')
		return (row + 2);
	return (row);
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
**
** A server the user is not looking at carries the unread count of every
** buffer it owns, console included: those tabs are on the bottom bar and
** nothing else would tell the user their server has news.
*/
static int		server_chips(t_syschat *syschat, t_chip *chips)
{
	int	n;

	n = 0;
	for (int i = 0; i != syschat->conn_count && n < SYSCHAT_MAX_SERVERS; i++)
	{
		int	unread;

		if (!syschat->conns[i].alive)
			continue ;
		chips[n].label = syschat->conns[i].label ? syschat->conns[i].label : "?";
		unread = 0;
		if (i != syschat->current)
		{
			for (int j = 0; j != syschat->buffer_count; j++)
				if (syschat->buffers[j].server_id == (unsigned) i)
					unread += syschat->buffers[j].unread;
		}
		chips[n].unread = unread;
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

/*
** The prompt is wrapped like a channel line: the prefix starts the line, so
** the first row carries it and every continuation row is indented by
** wrap_line() exactly like a wrapped message. That is what keeps the text
** being typed readable once it reaches the right edge, instead of vanishing.
** The caller decides how many rows the screen can hold.
*/
static int	prompt_wrap(t_syschat *syschat, const char *input, int cols,
	char ***out)
{
	const char	*nick;
	char		*line;
	size_t		need;
	int			count;

	*out = NULL;
	nick = SYSC(syschat)->nickname ? SYSC(syschat)->nickname : "";
	need = strlen(nick) + strlen(input ? input : "") + 96;
	if (syschat->active >= 0 && syschat->active < syschat->buffer_count)
		need += strlen(syschat->buffers[syschat->active].name);
	line = malloc(need);
	if (!line)
		return (0);
	if (syschat->active >= 0 && syschat->active < syschat->buffer_count)
		snprintf(line, need, "\033[90m[%s]\033[0m \033[1;36m<%s>\033[0m %s",
			syschat->buffers[syschat->active].name, nick,
			input ? input : "");
	else
		snprintf(line, need, "> %s", input ? input : "");
	count = wrap_line(line, cols - 1, out);
	free(line);
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
	size_t	size;
	size_t	used;
	size_t	written;
	char	**rows;
	char	**prows;
	int		pcount;
	int		prompt_rows;
	int		prompt_max;
	int		first;
	int		nrows;
	int		top_cut;
	int		rows_count;
	int		cols;
	int		room;
	int		index;
	int		nchips;
	int		buffer_rows;
	char	right[32];
	t_chip	chips[SYSCHAT_MAX_BUFFERS];
	t_buffer	*buffer;

	rows_count = screen_height();
	cols = screen_width();
	/*
	** One row of the frame is at most 4 * cols + 256 bytes: a wrapped row is
	** bounded by wrap_line()'s own storage (4 bytes per visible column, plus
	** the ANSI it re-emits), and a bar stops at `cols` visible columns. The
	** frame never holds more than rows_count rows -- two bars, the messages,
	** the prompt -- because the prompt takes its height out of `room`. That
	** bound is what keeps every `size - used` below honest: a multi-byte,
	** ANSI-heavy or wrapped row cannot be larger than one row budget.
	*/
	size = (size_t)(rows_count + 2) * (size_t)(4 * cols + 256) + 1024;
	out = calloc(size, sizeof(char));
	if (!out)
		return ;
	used = 0;
	out_add(out, size, &used, "\033[H");
	/*
	** Two bars: servers on top, then the buffers of the current server. The
	** bottom bar is skipped when the current server has none (its console is
	** the only tab), so the conversation keeps that row.
	*/
	nchips = server_chips(syschat, chips);
	if (nchips)
	{
		snprintf(right, sizeof(right), "%d/%d srv", nchips, SYSCHAT_MAX_SERVERS);
		written = chips_bar(chips, nchips, right, out + used, size - used, cols);
		used = written < size - used ? used + written : size - 1;
	}
	else
		out_add(out, size, &used, "%s-- no server --%s", TAB_IDLE, RESET);
	out_add(out, size, &used, "\033[K\r\n");
	buffer_rows = 0;
	nchips = buffer_chips(syschat, chips);
	if (nchips)
	{
		snprintf(right, sizeof(right), "%d buf%s", nchips, nchips > 1 ? "s" : "");
		written = chips_bar(chips, nchips, right, out + used, size - used, cols);
		used = written < size - used ? used + written : size - 1;
		out_add(out, size, &used, "\033[K\r\n");
		buffer_rows = 1;
	}
	/*
	** The prompt is the last block and grows downwards: one more row each
	** time the input reaches the right edge, which is why `room` is computed
	** from its height instead of the other way round. It is capped so the
	** frame can never scroll -- the bar(s) and at least one message row
	** always survive, and the prompt never eats more than half the terminal.
	** When it is capped the FIRST rows go: the cursor and the text being
	** typed right now are at the end, that is what must stay visible.
	*/
	pcount = prompt_wrap(syschat, input, cols, &prows);
	if (!prows)
		pcount = 1;
	prompt_max = rows_count - 2 - buffer_rows;
	if (prompt_max > rows_count / 2)
		prompt_max = rows_count / 2;
	if (prompt_max < 1)
		prompt_max = 1;
	prompt_rows = pcount < prompt_max ? pcount : prompt_max;
	if (prompt_rows < 1)
		prompt_rows = 1;
	room = rows_count - 2 - buffer_rows - (prompt_rows - 1);
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
				out_add(out, size, &used, "\033[90m..\033[0m%s\033[K\r\n",
					continuation_body(rows[i]));
			else
				out_add(out, size, &used, "%s\033[K\r\n", rows[i]);
			free(rows[i]);
		}
		free(rows);
		for (int i = nrows; i < room; i++)
			out_add(out, size, &used, "\033[K\r\n");
	}
	else if (syschat->buffer_count == 0 && room > 0)
	{
		/* Started with no argument: say what to do, then whatever the last
		** commands answered. They cannot go into a buffer (there is none). */
		int	line;
		int	top;

		out_add(out, size, &used,
			"\033[0;37mNo server yet. /connect <nick> <server> <host> [port] to open one, "
				"/? for help.\033[0m\033[K\r\n");
		line = 1;
		/* The ring is right aligned: the oldest entry sits at
		** SYSCHAT_STATUS_LINES - status_count, not at 0. When the ring holds
		** more lines than the window, keep the NEWEST ones: the last answer
		** is the one the user is waiting for. */
		top = SYSCHAT_STATUS_LINES - syschat->status_count;
		if (SYSCHAT_STATUS_LINES - top > room - 1)
			top = SYSCHAT_STATUS_LINES - (room - 1);
		for (int i = top; i != SYSCHAT_STATUS_LINES && line < room; i++, line++)
			out_add(out, size, &used, "%s\033[K\r\n",
				syschat->status[i] ? syschat->status[i] : "");
		for (; line < room; line++)
			out_add(out, size, &used, "\033[K\r\n");
	}
	else
		for (int i = 0; i != room; i++)
			out_add(out, size, &used, "\033[K\r\n");
	/* The prompt rows are the tail of the wrapped line, so the last one holds
	** the cursor and no newline follows it: the frame stays exactly
	** rows_count rows tall and never scrolls itself. */
	first = pcount - prompt_rows;
	if (first < 0)
		first = 0;
	for (int i = first; i != pcount; i++)
	{
		if (i == first && first > 0)
			out_add(out, size, &used, "\033[90m..\033[0m%s",
				continuation_body(prows[i]));
		else
			out_add(out, size, &used, "%s", prows ? prows[i] : "");
		if (i + 1 != pcount)
			out_add(out, size, &used, "\033[K\r\n");
		else
			out_add(out, size, &used, "\033[K");
	}
	if (prows)
	{
		for (int i = 0; i != pcount; i++)
			free(prows[i]);
		free(prows);
	}
	write(1, out, used);
	free(out);
}
