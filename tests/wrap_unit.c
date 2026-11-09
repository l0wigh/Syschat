/*
** Unit test for the line wrapping in screen.c.
**
** screen.c is included rather than linked: wrap_line() is static and pulling a
** whole renderer into a test binary would need a terminal. Including the
** translation unit keeps the function private and the test runnable anywhere.
**
** Build (from the repository root):
**   gcc -std=c11 -Wall -Wextra -I. -o tests/wrap_unit tests/wrap_unit.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../srcs/utils/screen.c"

static int	g_fail;

static void	check(const char *name, int ok)
{
	printf("%s   %s\n", ok ? "OK  " : "FAIL", name);
	if (!ok)
		g_fail = 1;
}

/* Sum of the visible columns of every row. */
static int	rows_width(char **rows, int count)
{
	int	total;

	total = 0;
	for (int i = 0; i != count; i++)
		total += screen_text_width(rows[i]);
	return (total);
}

static void	free_rows(char **rows, int count)
{
	for (int i = 0; i != count; i++)
		free(rows[i]);
	free(rows);
}

/* A row must never end in the middle of a UTF-8 character. */
static int	rows_utf8_whole(char **rows, int count)
{
	for (int i = 0; i != count; i++)
	{
		size_t	len = strlen(rows[i]);

		if (len && ((unsigned char)rows[i][len - 1] & 0xC0) == 0x80)
		{
			size_t	lead = len;

			while (lead > 0 && ((unsigned char)rows[i][lead - 1] & 0xC0) == 0x80)
				lead--;
			if (lead == 0)
				return (0);
			if ((size_t)utf8_len(rows[i] + lead - 1) != len - lead + 1)
				return (0);
		}
	}
	return (1);
}

int	main(void)
{
	char	line[1200];
	char	**rows;
	int		count;
	int		width;

	/* 1100 plain characters, wrapped to 39 columns: 30 rows, each one inside
	** the width, and the indent of the 29 continuation rows accounted for. */
	memset(line, 'a', 1100);
	line[1100] = '\0';
	count = wrap_line(line, 39, &rows);
	check("1100 chars wrap into 30 rows", count == 30);
	check("more rows than a 24-row terminal has room for", count > 22);
	check("every row fits the requested width",
		rows_width(rows, count) - 2 * (count - 1) == 1100);
	{
		int	too_wide = 0;

		for (int i = 0; i != count; i++)
			if (screen_text_width(rows[i]) > 39)
				too_wide = 1;
		check("no row exceeds 39 columns", !too_wide);
	}
	free_rows(rows, count);

	/* Multi-byte characters: 120 'é' over 39 columns -> 39/39/39/9. */
	for (int i = 0; i != 120; i++)
	{
		line[i * 2] = (char)0xC3;
		line[i * 2 + 1] = (char)0xA9;
	}
	line[240] = '\0';
	count = wrap_line(line, 39, &rows);
	check("120 accented characters wrap into 4 rows", count == 4);
	width = screen_text_width(rows[count - 1]);
	check("last accented row is 9 columns", width == 9);
	check("no character is split between two rows", rows_utf8_whole(rows, count));
	check("all 120 characters are still there",
		rows_width(rows, count) - 2 * (count - 1) == 120);
	free_rows(rows, count);

	/* A short line stays on a single row and gets no indent. */
	count = wrap_line("bonjour", 39, &rows);
	check("a short line is one row", count == 1);
	check("a short line is not indented", screen_text_width(rows[0]) == 7);
	free_rows(rows, count);

	return (g_fail);
}
