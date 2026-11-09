#include "server.h"
#include "buffers.h"
#include "ctcp.h"
#include <sys/socket.h>

static const char *MANAGED_COMMANDS[] = {
	"PING",
	"JOIN",
	"QUIT",
	"PART",
	"PRIVMSG",
	"NICK",
	"KICK",
	"MODE",
	"NOTICE",
	NULL
};

static void	target_chan(char *target, size_t size, char *value)
{
	char	*cr;

	if (!value)
		return ;
	snprintf(target, size, "%s", value);
	cr = strchr(target, '\r');
	if (cr)
		*cr = '\0';
}

void server_handle_ping(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	char		message[BF_SIZE];
	const char	*token;

	token = "";
	for (int i = 0; i != 4; i++)
		if (strcmp(parsed[i], "PING") == 0)
		{
			token = parsed[i + 1];
			break ;
		}
	bzero(message, BF_SIZE);
	bzero(srv_message, BF_SIZE);
	bzero(target, SYSCHAT_NAME_LEN);
	snprintf(message, BF_SIZE, "PONG :%s\r\n", token);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
}

void server_handle_join(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	int	index;

	bzero(srv_message, BF_SIZE);
	target_chan(target, SYSCHAT_NAME_LEN, parsed[2]);
	if (syschat->nickname && strcasecmp(parsed[0], syschat->nickname) == 0)
	{
		index = buffers_join(syschat, target);
		if (index != -1)
			syschat->buffers[index].joined = 1;
	}
	snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m just \e[0;32mentered\e[0m",
		target, parsed[0]);
}

void server_handle_quit(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	target_chan(target, SYSCHAT_NAME_LEN, syschat->hostname);
	bzero(srv_message, BF_SIZE);
	snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m \e[0;31mdisconnected\e[0m",
		syschat->hostname, parsed[0]);
}

void server_handle_part(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	int	index;

	bzero(srv_message, BF_SIZE);
	target_chan(target, SYSCHAT_NAME_LEN, parsed[2]);
	if (syschat->nickname && strcasecmp(parsed[0], syschat->nickname) == 0)
	{
		index = buffers_find(syschat, target);
		if (index != -1)
			syschat->buffers[index].joined = 0;
	}
	snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m just \e[0;31mleft\e[0m",
		target, parsed[0]);
}

void server_handle_privmsg(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	bzero(srv_message, BF_SIZE);
	if (parsed[2][0] == '#')
	{
		target_chan(target, SYSCHAT_NAME_LEN, parsed[2]);
		snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] <\e[0;34m%s\e[0m>: \e[0;34m%s\e[0m",
			target, parsed[0], parsed[3]);
	}
	else
	{
		target_chan(target, SYSCHAT_NAME_LEN, parsed[0]);
		if (parsed[3][0] == 1)
		{
			ctcp_handle_request(syschat, parsed);
			snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] Asked for CTCP: \e[0;36m%s\e[0m",
				parsed[0], parsed[3]);
			return ;
		}
		snprintf(srv_message, BF_SIZE, "<\e[0;34m%s\e[0m> -> <\e[0;33m%s\e[0m>: \e[0;34m%s\e[0m",
			parsed[0], syschat->nickname, parsed[3]);
	}
}

void server_handle_nick(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	target_chan(target, SYSCHAT_NAME_LEN, syschat->hostname);
	bzero(srv_message, BF_SIZE);
	snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] \e[0;31m%s\e[0m is now \e[0;32m%s\e[0m",
		syschat->hostname, parsed[0], parsed[2]);
	if (syschat->nickname && strcasecmp(parsed[0], syschat->nickname) == 0)
	{
		free(syschat->nickname);
		syschat->nickname = strdup(parsed[2]);
	}
}

void server_handle_kick(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	(void) syschat;
	bzero(srv_message, BF_SIZE);
	target_chan(target, SYSCHAT_NAME_LEN, parsed[2]);
	snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m kicked \e[0;31m%s\e[0m",
		target, parsed[0], parsed[3]);
}

void server_handle_mode(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	bzero(srv_message, BF_SIZE);
	target_chan(target, SYSCHAT_NAME_LEN, parsed[2]);
	snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m changed \e[0;35m%s\e[0m modes to \e[0;36m%s\e[0m",
		syschat->hostname, parsed[0], target, parsed[3]);
}

void server_handle_notice(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	(void) syschat;
	bzero(srv_message, BF_SIZE);
	if (parsed[2][0] == '#')
		target_chan(target, SYSCHAT_NAME_LEN, parsed[2]);
	else
		target_chan(target, SYSCHAT_NAME_LEN, parsed[0]);
	if (parsed[3][0] == 1)
		snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] CTCP answer: \e[0;36m%s\e[0m",
			parsed[0], parsed[3]);
	else
		snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] -\e[0;36m%s\e[0m- %s",
			target, parsed[0], parsed[3]);
}

/*
** Returns 1 when `srv_message` holds something to display, 0 otherwise.
** `target` receives the buffer name the message belongs to.
*/
int server_handle_message(t_syschat *syschat, char *srv_message, char *target)
{
	char	**parsed;
	int		displayed;
	int		known;

	displayed = 0;
	bzero(target, SYSCHAT_NAME_LEN);
	known = 0;
	for (int i = 0; MANAGED_COMMANDS[i]; i++)
		if (strstr(srv_message, MANAGED_COMMANDS[i]))
			known = 1;
	if (!known)
	{
		target_chan(target, SYSCHAT_NAME_LEN, syschat->hostname);
		return (0);
	}
	parsed = server_parse_message(srv_message);
	if (strcmp(parsed[0], "PING") == 0 || strcmp(parsed[1], "PING") == 0)
		server_handle_ping(syschat, parsed, srv_message, target);
	else if (strcmp(parsed[1], "JOIN") == 0)
		server_handle_join(syschat, parsed, srv_message, target);
	else if (strcmp(parsed[1], "QUIT") == 0)
		server_handle_quit(syschat, parsed, srv_message, target);
	else if (strcmp(parsed[1], "PART") == 0)
		server_handle_part(syschat, parsed, srv_message, target);
	else if (strcmp(parsed[1], "PRIVMSG") == 0)
		server_handle_privmsg(syschat, parsed, srv_message, target);
	else if (strcmp(parsed[1], "NICK") == 0)
		server_handle_nick(syschat, parsed, srv_message, target);
	else if (strcmp(parsed[1], "KICK") == 0)
		server_handle_kick(syschat, parsed, srv_message, target);
	else if (strcmp(parsed[1], "MODE") == 0)
		server_handle_mode(syschat, parsed, srv_message, target);
	else if (strcmp(parsed[1], "NOTICE") == 0)
		server_handle_notice(syschat, parsed, srv_message, target);
	displayed = srv_message[0] != '\0';
	for (int i = 0; i != 16; i++)
		free(parsed[i]);
	free(parsed);
	return (displayed);
}

char **server_parse_message(char *srv_message)
{
	char **res;
	int counter = 0;
	int elem = 0;
	int ch = 0;
	int len = 0;
	int is_nickname = 1;
	int still_skip = 1;

	res = (char **) calloc(16, sizeof(char *));
	for (int i = 0; i != 16; i++)
		res[i] = (char *) calloc(BF_SIZE + 1, sizeof(char));

	len = strlen(srv_message);
	if (srv_message[0] == ':')
	{
		still_skip = 1;
		counter++;
	}
	while (counter < len && srv_message[counter] != '\r')
	{
		if (elem > 15 || ch > 511)
			break;
		if (srv_message[counter] == '!' && is_nickname)
		{
			while (counter < len && srv_message[counter] != ' ')
				counter++;
			is_nickname = 0;
			elem++;
			ch = -1;
		}
		else if (srv_message[counter] == ' ' && still_skip)
		{
			elem++;
			ch = -1;
		}
		else if (srv_message[counter] == ':' && ch == 0 && still_skip)
		{
			still_skip = 0;
			ch = -1;
		}
		else if (ch >= 0 && ch < BF_SIZE)
			res[elem][ch] = srv_message[counter];
		counter++;
		ch++;
	}
	return res;
}
