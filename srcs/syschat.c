#include "syschat.h"
#include "utils/network.h"
#include "utils/tls.h"
#include "utils/error.h"
#include "utils/commands.h"
#include "utils/server.h"
#include "utils/buffers.h"
#include "utils/screen.h"
#include "segfault_catcher/segfault_catcher.h"

static t_syschat syschat;
static struct termios oldt, newt;
static char input[SYSCHAT_LINE_LEN];
static int input_len = 0;

#define NET_BUFFER_SIZE 16384

// TODO: Fix stack smashing when pasting too much characters
// TODO: Use write instead of printf when it's possible

/*
** Flags are recognised anywhere on the command line, positionals keep their
** historic order (<hostname> <nickname> [port]) so every documented command
** still works unchanged. TLS is never on by default on 6667: it is enabled
** by --tls, or implicitly when the port is 6697.
*/
void syschat_load_config(char **argv, int argc)
{
	char	*positional[3];
	char	*env;
	int		i;
	int		pos;
	int		force_tls;
	int		no_tls;

	bzero(&syschat, sizeof(t_syschat));
	bzero(positional, sizeof(positional));
	pos = 0;
	force_tls = 0;
	no_tls = 0;
	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--tls") || !strcmp(argv[i], "-tls"))
			force_tls = 1;
		else if (!strcmp(argv[i], "--no-tls") || !strcmp(argv[i], "-no-tls"))
			no_tls = 1;
		else if (!strcmp(argv[i], "--insecure") || !strcmp(argv[i], "-insecure"))
			syschat.tls_insecure = 1;
		else if (pos < 3)
			positional[pos++] = argv[i];
	}
	if (pos < 2)
		error_exit(NULL, 1);
	syschat.hostname = strdup(positional[0]);
	syschat.nickname = strdup(positional[1]);
	if (pos > 2)
	{
		char	*end;
		long	parsed;

		end = NULL;
		parsed = strtol(positional[2], &end, 10);
		if (end == positional[2] || *end != '\0' || parsed < 1 || parsed > 65535)
			error_exit(NULL, 9);
		syschat.port = (int) parsed;
	}
	else
		syschat.port = 6667;
	syschat.tls = (syschat.port == 6697);
	if ((env = getenv("SYSCHAT_TLS")) != NULL && env[0] != '\0')
		syschat.tls = !strcmp(env, "1") || !strcmp(env, "true") || !strcmp(env, "yes");
	if ((env = getenv("SYSCHAT_TLS_NO_VERIFY")) != NULL
		&& (!strcmp(env, "1") || !strcmp(env, "true") || !strcmp(env, "yes")))
		syschat.tls_insecure = 1;
	if (force_tls)
		syschat.tls = 1;
	if (no_tls)
		syschat.tls = 0;
	if (syschat.tls_insecure && !no_tls)
		syschat.tls = 1;
	syschat.active = -1;
	buffers_new(&syschat, syschat.hostname);
	buffers_switch(&syschat, 0);
}

void syschat_say_hello(void)
{
	char message[BF_SIZE];

	net_send(&syschat, "PASS none\r\n", 11);

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "NICK %s\r\n", syschat.nickname);
	net_send(&syschat, message, strlen(message));

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "USER %s %s %s %s\r\n", syschat.nickname, syschat.nickname,
		syschat.nickname, syschat.nickname);
	net_send(&syschat, message, strlen(message));
}

void syschat_echo(char *text)
{
	char line[BF_SIZE * 2];

	bzero(line, sizeof(line));
	snprintf(line, sizeof(line), "%s", text);
	buffers_append(&syschat, syschat.active, line);
}

static void syschat_send_line(char *input)
{
	char		*name;
	char		line[BF_SIZE * 2];
	const char	*sent;
	int			truncated;

	name = buffers_active_name(&syschat);
	if (!name || syschat.active <= 0)
	{
		syschat_echo("\e[0;31mNot in a channel or a private message. Use /join <channel>\e[0m");
		return ;
	}
	sent = send_privmsg(&syschat, name, input, &truncated);
	if (!sent)
		return ;
	bzero(line, sizeof(line));
	if (name[0] == '#')
		snprintf(line, sizeof(line), "<%s%s\e[0m> %s",
			nick_color(syschat.nickname), syschat.nickname, sent);
	else
		snprintf(line, sizeof(line), "<%s%s\e[0m> -> <%s%s\e[0m> %s",
			nick_color(syschat.nickname), syschat.nickname, nick_color(name), name, sent);
	buffers_append(&syschat, syschat.active, line);
	if (truncated)
	{
		bzero(line, sizeof(line));
		snprintf(line, sizeof(line), "\e[0;31mmessage tronqué à %d/%d caractères (limite serveur 512 octets)\e[0m",
			screen_text_width(sent), screen_text_width(input));
		buffers_append(&syschat, syschat.active, line);
	}
}

/*
** Byte oriented input handling: keeps reading possible even when a whole
** escape sequence (Shift-Tab = ESC [ Z) is split across two reads.
*/
void syschat_handle_input(char *buffer, int size)
{
	static int	escape = 0;
	static int	input_full = 0;
	int			index;

	for (index = 0; index != size; index++)
	{
		char c = buffer[index];

		if (escape == 1)
		{
			escape = (c == '[' || c == 'O') ? 2 : 0;
			continue ;
		}
		if (escape == 2)
		{
			escape = 0;
			if (c == 'Z')
				buffers_cycle(&syschat, -1);
			continue ;
		}
		if (c == '\033')
			escape = 1;
		else if (c == '\t')
			buffers_cycle(&syschat, 1);
		else if (c == 127 || c == 8)
		{
			/* Step back over the whole UTF-8 character, not one byte. */
			if (input_len > 0)
				input_len--;
			while (input_len > 0 && ((unsigned char) input[input_len] & 0xC0) == 0x80)
				input_len--;
			input[input_len] = '\0';
		}
		else if (c == '\n' || c == '\r')
		{
			input[input_len] = '\0';
			if (input_len > 0)
			{
				if (input[0] == '/')
					commands_execute(&syschat, input);
				else
					syschat_send_line(input);
			}
			input_len = 0;
			input[0] = '\0';
			input_full = 0;
		}
		else if ((unsigned char) c >= 32)
		{
			if (input_len + 1 >= SYSCHAT_LINE_LEN)
			{
				/* A paste longer than the line buffer used to vanish here,
				** silently, before the send path could warn about it. */
				if (!input_full)
				{
					input_full = 1;
					char	warn[128];

					snprintf(warn, sizeof(warn),
						"\e[0;31minput full: %d characters max, the rest is ignored\e[0m",
						SYSCHAT_LINE_LEN - 1);
					buffers_append(&syschat, syschat.active, warn);
				}
			}
			else
			{
				input[input_len++] = c;
				input[input_len] = '\0';
			}
		}
	}
	screen_draw(&syschat, input);
}

/*
** The socket is drained into a persistent line accumulator: a chunk can
** hold several IRC lines, or half of one. Nothing is dropped.
*/
void syschat_read_network(void)
{
	static char	net[NET_BUFFER_SIZE];
	static int	net_len = 0;
	char		srv[BF_SIZE];
	char		target[SYSCHAT_NAME_LEN];
	char		*end;
	char		chunk[2048];
	int			received;
	int			line_len;
	int			index;

	bzero(chunk, sizeof(chunk));
	received = net_recv(&syschat, chunk, sizeof(chunk) - 1);
	if (received == -1)
		return ;
	if (received <= 0)
	{
		syschat.running = 0;
		return ;
	}
	if (received > NET_BUFFER_SIZE - 1 - net_len)
		net_len = 0;
	memcpy(net + net_len, chunk, received);
	net_len += received;
	net[net_len] = '\0';
	while ((end = strchr(net, '\n')) != NULL)
	{
		line_len = end - net;
		while (line_len > 0 && (net[line_len - 1] == '\r' || net[line_len - 1] == '\0'))
			line_len--;
		if (line_len > 0)
		{
			bzero(srv, sizeof(srv));
			snprintf(srv, sizeof(srv), "%.*s", line_len, net);
			bzero(target, SYSCHAT_NAME_LEN);
			if (server_handle_message(&syschat, srv, target))
			{
				if (target[0] == '\0')
					snprintf(target, SYSCHAT_NAME_LEN, "%s", syschat.hostname);
				index = buffers_route(&syschat, target);
				buffers_append(&syschat, index, srv);
			}
		}
		memmove(net, end + 1, net_len - (end + 1 - net) + 1);
		net_len -= (end + 1 - net);
	}
	screen_draw(&syschat, input);
}

void syschat_loop(void)
{
	char			buffer[512];
	struct pollfd	fds[2];
	int				ret;

	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;
	fds[1].fd = syschat.net_socket;
	fds[1].events = POLLIN;
	syschat.running = 1;
	while (syschat.running)
	{
		fds[0].revents = 0;
		fds[1].revents = 0;
		/* With TLS a record can hold more bytes than one chunk, so the
		** socket itself may stay silent while decrypted data is waiting. */
		ret = poll(fds, 2, net_pending(&syschat) ? 0 : 500);
		if (ret < 0)
			continue ;
		if (fds[0].revents & POLLIN)
		{
			ret = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
			if (ret > 0)
				syschat_handle_input(buffer, ret);
		}
		if (net_pending(&syschat) || (fds[1].revents & (POLLIN | POLLHUP | POLLERR)))
			syschat_read_network();
		if (syschat.listing && time(NULL) - syschat.listing_since >= 5)
		{
			int index = buffers_find(&syschat, "Liste");

			syschat.listing = 0;
			if (index != -1)
				buffers_append(&syschat, index,
					"\e[0;31m-- listing stopped after 5s, server sent nothing else\e[0m");
			screen_draw(&syschat, input);
		}
	}
}

int main(int argc, char **argv)
{
	segfault_catcher_set(0);

	if (argc < 3)
		error_exit(NULL, 1);

	syschat_load_config(argv, argc);
	screen_setup(&syschat);

	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	newt.c_oflag &= ~OPOST;
	newt.c_cc[VMIN] = 1;
	newt.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	network_connect_server(&syschat);
	syschat_say_hello();

	{
		char	banner[256];
		char	info[160];

		tls_summary(&syschat, info, sizeof(info));
		snprintf(banner, sizeof(banner),
			"Connected (%s). /join #channel to start, Tab / Shift-Tab to switch, /? for help.",
			info);
		syschat_echo(banner);
	}
	screen_draw(&syschat, input);
	syschat_loop();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	error_cleaning(&syschat);
	return (0);
}
