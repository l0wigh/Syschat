#include "syschat.h"
#include "utils/network.h"
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

void syschat_load_config(char **argv, int argc_port)
{
	bzero(&syschat, sizeof(t_syschat));
	syschat.hostname = strdup(argv[1]);
	syschat.nickname = strdup(argv[2]);
	if (argc_port > 3)
	{
		char	*end;
		long	parsed;

		end = NULL;
		parsed = strtol(argv[3], &end, 10);
		if (end == argv[3] || *end != '\0' || parsed < 1 || parsed > 65535)
			error_exit(NULL, 9);
		syschat.port = (int) parsed;
	}
	else
		syschat.port = 6667;
	syschat.active = -1;
	buffers_new(&syschat, syschat.hostname);
	buffers_switch(&syschat, 0);
}

void syschat_say_hello(void)
{
	char message[BF_SIZE];

	send(syschat.net_socket, "PASS none\r\n", 11, MSG_DONTWAIT);

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "NICK %s\r\n", syschat.nickname);
	send(syschat.net_socket, message, strlen(message), MSG_DONTWAIT);

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "USER %s %s %s %s\r\n", syschat.nickname, syschat.nickname,
		syschat.nickname, syschat.nickname);
	send(syschat.net_socket, message, strlen(message), MSG_DONTWAIT);
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
	char	*name;
	char	message[BF_SIZE];
	char	line[BF_SIZE * 2];

	name = buffers_active_name(&syschat);
	if (!name || syschat.active <= 0)
	{
		syschat_echo("\e[0;31mNot in a channel or a private message. Use /join <channel>\e[0m");
		return ;
	}
	if (strlen(input) > BF_SIZE - 16)
		return ;
	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "PRIVMSG %s :%s\r\n", name, input);
	send(syschat.net_socket, message, strlen(message), MSG_DONTWAIT);
	bzero(line, sizeof(line));
	if (name[0] == '#')
		snprintf(line, sizeof(line), "[\e[0;35m%s\e[0m] <\e[0;33m%s\e[0m>: \e[0;33m%s\e[0m",
			name, syschat.nickname, input);
	else
		snprintf(line, sizeof(line), "\e[0;33m%s\e[0m -> \e[0;34m%s\e[0m: \e[0;33m%s\e[0m",
			syschat.nickname, name, input);
	buffers_append(&syschat, syschat.active, line);
}

/*
** Byte oriented input handling: keeps reading possible even when a whole
** escape sequence (Shift-Tab = ESC [ Z) is split across two reads.
*/
void syschat_handle_input(char *buffer, int size)
{
	static int	escape = 0;
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
			if (input_len > 0)
				input[--input_len] = '\0';
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
		}
		else if (c >= 32 && input_len + 1 < SYSCHAT_LINE_LEN)
		{
			input[input_len++] = c;
			input[input_len] = '\0';
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
	received = recv(syschat.net_socket, chunk, sizeof(chunk) - 1, 0);
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
		ret = poll(fds, 2, 500);
		if (ret < 0)
			continue ;
		if (fds[0].revents & POLLIN)
		{
			ret = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
			if (ret > 0)
				syschat_handle_input(buffer, ret);
		}
		if (fds[1].revents & (POLLIN | POLLHUP | POLLERR))
			syschat_read_network();
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

	syschat_echo("Connected. /join #channel to start, Tab / Shift-Tab to switch, /? for help.");
	screen_draw(&syschat, input);
	syschat_loop();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	error_cleaning(&syschat);
	return (0);
}
