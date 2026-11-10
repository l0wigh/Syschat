/*
** Unit test for the wrapped input prompt of screen.c.
**
** screen_draw() is exercised for real: the translation unit is included so the
** static helpers come along, TIOCGWINSZ is faked (a test binary has no
** terminal of a chosen size) and fd 1 is redirected to a file to read the
** frame back. Nothing else is stubbed.
**
** Scope: the prompt wraps instead of being cut, the frame keeps exactly
** screen_height() rows (so it never scrolls), the cursor is left on the last
** prompt row, no character is split, the prompt is capped and keeps its tail.
**
** Build (from the repository root):
**   gcc -std=c11 -Wall -Wextra -Werror -I. -o tests/prompt_wrap_unit tests/prompt_wrap_unit.c
*/
#ifndef _DEFAULT_SOURCE
# define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

static int	g_cols = 80;
static int	g_rows = 24;
static int	g_have_tty = 1;

static int	fake_ioctl(int fd, unsigned long request, ...);

/* screen.c asks the terminal its size; a test answers instead. */
#define ioctl fake_ioctl
#include "../srcs/utils/screen.c"

static int	fake_ioctl(int fd, unsigned long request, ...)
{
	va_list			ap;
	struct winsize	*ws;

	(void) fd;
	(void) request;
	va_start(ap, request);
	ws = va_arg(ap, struct winsize *);
	va_end(ap);
	if (!g_have_tty || !ws)
		return (-1);
	ws->ws_col = (unsigned short) g_cols;
	ws->ws_row = (unsigned short) g_rows;
	ws->ws_xpixel = 0;
	ws->ws_ypixel = 0;
	return (0);
}

static int	g_fail;

static void	check(const char *name, int ok)
{
	printf("%s   %s\n", ok ? "OK  " : "FAIL", name);
	if (!ok)
		g_fail = 1;
}

/* ------------------------------------------------------------------ frame */

#define FRAME_MAX	(1 << 21)
#define LINES_MAX	256

static char		g_frame[FRAME_MAX];
static char		*g_line[LINES_MAX];
static int		g_lines;

static char	*strip_ansi(const char *s)
{
	size_t	len;
	size_t	used;
	char	*dst;

	len = strlen(s);
	dst = calloc(len + 1, sizeof(char));
	if (!dst)
		return (NULL);
	used = 0;
	for (size_t i = 0; i < len; i++)
	{
		if (s[i] == '\033')
		{
			while (i < len && s[i] != 'm')
				i++;
			continue ;
		}
		dst[used++] = s[i];
	}
	return (dst);
}

static void	split_frame(void)
{
	char	*cur;
	int		i;

	for (i = 0; i != g_lines; i++)
		free(g_line[i]);
	g_lines = 0;
	cur = g_frame;
	for (i = 0; g_frame[i] && g_lines < LINES_MAX; i++)
	{
		if (g_frame[i] == '\r' && g_frame[i + 1] == '\n')
		{
			g_frame[i] = '\0';
			g_line[g_lines++] = strip_ansi(cur);
			cur = g_frame + i + 2;
			i++;
		}
	}
	g_line[g_lines++] = strip_ansi(cur);
}

/* Draw one frame of `input` at the size set in g_cols/g_rows. */
static void	draw(t_syschat *syschat, const char *input)
{
	char	path[] = "/tmp/syschat_prompt_XXXXXX";
	ssize_t	got;
	int		saved;
	int		fd;

	saved = dup(STDOUT_FILENO);
	fd = mkstemp(path);
	if (saved < 0 || fd < 0)
	{
		printf("FAIL   cannot redirect stdout\n");
		g_fail = 1;
		return ;
	}
	dup2(fd, STDOUT_FILENO);
	screen_draw(syschat, input);
	dup2(saved, STDOUT_FILENO);
	close(saved);
	lseek(fd, 0, SEEK_SET);
	g_frame[0] = '\0';
	got = read(fd, g_frame, sizeof(g_frame) - 1);
	g_frame[got > 0 ? (size_t) got : 0] = '\0';
	close(fd);
	unlink(path);
	split_frame();
}

/* Index of the first prompt row, or -1. */
static int	prompt_start(void)
{
	for (int i = 0; i != g_lines; i++)
		if (strstr(g_line[i], "[#a] <tester> ") || strncmp(g_line[i], "> ", 2) == 0
			|| strncmp(g_line[i], "..", 2) == 0)
			return (i);
	return (-1);
}

/*
** The prompt rows, concatenated, with the continuation indent removed. Equal
** to the prefix plus the whole input when nothing was dropped.
*/
static char	*prompt_text(void)
{
	size_t	used;
	int		from;
	char	*out;

	out = calloc(FRAME_MAX, sizeof(char));
	if (!out)
		return (NULL);
	used = 0;
	from = prompt_start();
	if (from < 0)
		return (out);
	for (int i = from; i != g_lines; i++)
	{
		const char	*row = g_line[i];
		size_t		len;

		if (i != from && row[0] == ' ' && row[1] == ' ')
			row += 2;
		len = strlen(row);
		if (used + len < FRAME_MAX)
		{
			memcpy(out + used, row, len);
			used += len;
		}
	}
	return (out);
}

static int	line_is_utf8(const char *s)
{
	int		left;
	unsigned char	c;

	left = 0;
	for (; *s; s++)
	{
		c = (unsigned char) *s;
		if (left)
		{
			if ((c & 0xC0) != 0x80)
				return (0);
			left--;
			continue ;
		}
		if (c < 0x80)
			continue ;
		if ((c & 0xE0) == 0xC0)
			left = 1;
		else if ((c & 0xF0) == 0xE0)
			left = 2;
		else if ((c & 0xF8) == 0xF0)
			left = 3;
		else
			return (0);
	}
	return (left == 0);
}

static int	all_lines_fit(int cols)
{
	for (int i = 0; i != g_lines; i++)
		if (!line_is_utf8(g_line[i]) || screen_text_width(g_line[i]) > cols)
			return (0);
	return (1);
}

/* ------------------------------------------------------------------ setup */

static void	setup(t_syschat *syschat)
{
	bzero(syschat, sizeof(t_syschat));
	syschat->conn_count = 1;
	syschat->current = 0;
	syschat->conns[0].alive = 1;
	syschat->conns[0].fd = 99;
	syschat->conns[0].nickname = "tester";
	syschat->conns[0].label = "srv";
	syschat->conns[0].hostname = "127.0.0.1";
	syschat->buffer_count = 1;
	syschat->active = 0;
	syschat->buffers[0].name = "#a";
	syschat->buffers[0].kind = BUF_CHANNEL;
	syschat->buffers[0].server_id = 0;
}

int	main(void)
{
	t_syschat	syschat;
	char		input[1200];
	char		*text;

	setup(&syschat);

	/* A short input: one prompt row, no indent, prefix intact. */
	g_cols = 80;
	g_rows = 24;
	draw(&syschat, "bonjour");
	check("a short input draws a full-height frame", g_lines == g_rows);
	check("a short input is one prompt row",
		prompt_start() == g_rows - 1 && strcmp(g_line[g_lines - 1],
			"[#a] <tester> bonjour") == 0);
	check("the frame does not end with a newline (cursor stays in the prompt)",
		!strstr(g_frame + strlen(g_frame) - 2, "\r\n"));

	/* 200 characters in an 80-column terminal: 3 rows, nothing lost. */
	for (int i = 0; i != 200; i++)
		input[i] = (char) ('a' + i % 26);
	input[200] = '\0';
	draw(&syschat, input);
	check("a 200-char input still draws a full-height frame", g_lines == g_rows);
	check("a 200-char input wraps over 3 rows", g_lines - prompt_start() == 3);
	{
		char	expect[256];

		snprintf(expect, sizeof(expect), "[#a] <tester> %s", input);
		text = prompt_text();
		check("prefix once, then the input, nothing dropped",
			text && strcmp(text, expect) == 0);
		free(text);
	}
	check("no row is wider than the terminal", all_lines_fit(g_cols));
	check("the prompt rows start where the messages end",
		prompt_start() > 0 && prompt_start() < g_rows);

	/* Narrow terminal, multi-byte input: no character may be split. */
	g_cols = 20;
	g_rows = 24;
	for (int i = 0; i != 120; i++)
	{
		input[i * 2] = (char) 0xC3;
		input[i * 2 + 1] = (char) 0xA9;
	}
	input[240] = '\0';
	draw(&syschat, input);
	text = prompt_text();
	{
		int	accents;

		accents = 0;
		for (const char *p = text ? text : ""; *p; p++)
			if ((unsigned char) *p >= 0xC0)
				accents++;
		check("120 accented characters at 20 columns are all displayed",
			accents == 120);
	}
	free(text);
	check("no row overflows a 20-column terminal", all_lines_fit(g_cols));
	check("no accented character is split across rows", all_lines_fit(g_cols));

	/* A paste: the prompt is capped, keeps its tail, and never scrolls. */
	g_cols = 80;
	g_rows = 24;
	memset(input, 'z', 1000);
	input[1000] = '\0';
	draw(&syschat, input);
	check("a 1000-char paste still draws a full-height frame", g_lines == g_rows);
	check("the prompt is capped at half the terminal",
		g_lines - prompt_start() <= g_rows / 2);
	check("the first visible prompt row says rows were dropped",
		prompt_start() > 0 && strncmp(g_line[prompt_start()], "..", 2) == 0);
	{
		char	full[1200];
		size_t	len;
		size_t	total;

		snprintf(full, sizeof(full), "[#a] <tester> %s", input);
		text = prompt_text();
		len = text ? strlen(text) : 0;
		total = strlen(full);
		/* prompt_text() keeps the ".." marker of the dropped rows. */
		check("the visible prompt is the END of what was typed",
			len > 2 && total > len - 2
				&& strcmp(text + 2, full + total - (len - 2)) == 0);
		free(text);
	}
	check("at least one message row survives the tallest prompt",
		prompt_start() >= 3);

	/* ANSI-heavy message lines on a narrow terminal: the frame buffer is
	** sized for wrap_line's own worst case, and must not be cut. */
	g_cols = 20;
	g_rows = 24;
	for (int i = 0; i != 6; i++)
	{
		char	*heavy = calloc(601, sizeof(char));

		for (int j = 0; j != 50; j++)
			memcpy(heavy + j * 12, "x\033[0;31mxx\033[0m", 12);
		syschat.buffers[0].lines[i] = heavy;
		syschat.buffers[0].count = i + 1;
		syschat.buffers[0].head = i + 1;
	}
	draw(&syschat, "hello");
	check("ANSI-heavy rows still produce a full-height frame", g_lines == g_rows);
	check("the prompt is still drawn after ANSI-heavy rows",
		prompt_start() > 0 && strcmp(g_line[g_lines - 1], "[#a] <tester> hello") == 0);
	check("no visible row overflows with ANSI-heavy content",
		all_lines_fit(g_cols));

	/* A stdout that cannot be asked its size: 80x24, still a valid frame. */
	g_have_tty = 0;
	draw(&syschat, "fallback");
	check("without a terminal the frame falls back to 80x24", g_lines == 24);
	g_have_tty = 1;

	for (int i = 0; i != g_lines; i++)
		free(g_line[i]);
	for (int i = 0; i != 6; i++)
		free(syschat.buffers[0].lines[i]);
	return (g_fail);
}
