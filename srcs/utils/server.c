#include "server.h"
static const int MANAGED_LEN = 9;
static const char *MANAGED_COMMANDS[9] = {
	"PING",
	"JOIN",
	"QUIT",
	"PART",
	"PRIVMSG",
	"NICK",
	"KICK",
	"MODE",
};

void server_handle_ping(t_syschat *syschat, char **parsed, char *srv_message)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	bzero(srv_message, BF_SIZE);
	sprintf(message, "PONG :%s\r\n", parsed[1]);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
}

void server_handle_join(t_syschat *syschat, char **parsed, char *srv_message)
{
	int notlocked;
	char *remove_backr;

	bzero(srv_message, BF_SIZE);
	remove_backr = strchr(parsed[2], '\r');
	if (remove_backr)
		*remove_backr = '\0';
	if (syschat->channel)
		notlocked = strcmp(parsed[2], syschat->channel);
	if (!notlocked)
		sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m just \e[0;32mentered\e[0m\n", parsed[2], parsed[0]);
	else
		sprintf(srv_message, "\e[0;37m[%s] %s just entered\e[0m\n", parsed[2], parsed[0]);
}

void server_handle_quit(t_syschat *syschat, char **parsed, char *srv_message)
{
	bzero(srv_message, BF_SIZE);
	sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m \e[0;31mdisconnected\e[0m\n", syschat->hostname, parsed[0]);
}

void server_handle_part(t_syschat *syschat, char **parsed, char *srv_message)
{
	int notlocked;
	char *remove_backr;

	bzero(srv_message, BF_SIZE);
	remove_backr = strchr(parsed[2], '\r');
	if (remove_backr)
		*remove_backr = '\0';
	if (syschat->channel)
		notlocked = strcmp(parsed[2], syschat->channel);
	if (!notlocked)
		sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m just \e[0;31mleft\e[0m\n", parsed[2], parsed[0]);
	else
		sprintf(srv_message, "\e[0;37m[%s] %s just left\e[0m\n", parsed[2], parsed[0]);
}

void server_handle_privmsg(t_syschat *syschat, char **parsed, char *srv_message)
{
	int notlocked = 1;

	bzero(srv_message, BF_SIZE);
	if (parsed[2][0] == '#')
	{
		if (syschat->channel)
			notlocked = strcmp(parsed[2], syschat->channel);
		if (!notlocked)
			sprintf(srv_message, "[\e[0;35m%s\e[0m] <\e[0;34m%s\e[0m>: \e[0;34m%s\e[0m", parsed[2], parsed[0], parsed[3]);
		else
			sprintf(srv_message, "\e[0;37m[%s] <%s>: %s\e[0m", parsed[2], parsed[0], parsed[3]);
	}
	else
	{
		if (syschat->channel)
			notlocked = strcmp(parsed[0], syschat->channel);
		if (!notlocked)
			sprintf(srv_message, "<\e[0;34m%s\e[0m> -> <\e[0;33m%s\e[0m>: \e[0;34m%s\e[0m", parsed[0], syschat->nickname, parsed[3]);
		else
			sprintf(srv_message, "\e[0;37m<%s> -> <%s>: %s\e[0m", parsed[0], syschat->nickname, parsed[3]);
	}
}

void server_handle_nick(t_syschat *syschat, char **parsed, char *srv_message)
{
	bzero(srv_message, BF_SIZE);
	sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;31m%s\e[0m is now \e[0;32m%s\e[0m", syschat->hostname, parsed[0], parsed[2]);
}

void server_handle_kick(t_syschat *syschat, char **parsed, char *srv_message)
{
	int notlocked;
	char *remove_backr;

	bzero(srv_message, BF_SIZE);
	remove_backr = strchr(parsed[2], '\r');
	if (remove_backr)
		*remove_backr = '\0';
	if (syschat->channel)
		notlocked = strcmp(parsed[2], syschat->channel);
	if (!notlocked)
		sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m kicked \e[0;31m%s\e[0m\n", parsed[2], parsed[0], parsed[3]);
	else
		sprintf(srv_message, "\e[0;37m[%s] %s kicked %s\e[0m\n", parsed[2], parsed[0], parsed[3]);
}

void server_handle_mode(t_syschat *syschat, char **parsed, char *srv_message)
{
	int notlocked;
	char *remove_backr;

	bzero(srv_message, BF_SIZE);
	remove_backr = strchr(parsed[3], '\r');
	if (remove_backr)
		*remove_backr = '\0';
	if (syschat->channel)
		notlocked = strcmp(parsed[2], syschat->channel);
	if (!notlocked)
		sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m changed \e[0;35m%s\e[0m modes to \e[0;36m%s\e[0m\n", syschat->hostname, parsed[0], parsed[2], parsed[3]);
	else
		sprintf(srv_message, "\e[0;37m[%s] %s changed %s modes to %s\e[0m\n", syschat->hostname, parsed[0], parsed[2], parsed[3]);
}

void server_handle_message(t_syschat *syschat, char *srv_message)
{
	char **parsed;
	char message[BF_SIZE];
	void *managed;

	for (int i = 0; i != MANAGED_LEN - 1; i++)
	{
		managed = strstr(srv_message, MANAGED_COMMANDS[i]);
		if (managed)
		{
			parsed = server_parse_message(srv_message);
			break;
		}
	}
	if (!managed)
		return;

	if (strcmp(parsed[0], "PING") == 0)
		server_handle_ping(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "JOIN") == 0)
		server_handle_join(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "QUIT") == 0)
		server_handle_quit(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "PART") == 0)
		server_handle_part(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "PRIVMSG") == 0)
		server_handle_privmsg(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "NICK") == 0)
		server_handle_nick(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "KICK") == 0)
		server_handle_kick(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "MODE") == 0)
		server_handle_mode(syschat, parsed, srv_message);
	else
		if (SYSCHAT_QUIET)
			bzero(srv_message, BF_SIZE);

	for (int i = 0; i != 16; i++)
		free(parsed[i]);
	free(parsed);
}

char **server_parse_message(char *srv_message)
{
	char **res;
	int counter = 0;
	int elem = 0;
	int ch = 0;
	int is_nickname = 1;
	int still_skip = 1;

	res = (char **) calloc(16, sizeof(char *));
	for (int i = 0; i != 16; i++)
		res[i] = (char *) calloc(BF_SIZE + 1, sizeof(char));

	if (srv_message[0] == ':')
	{
		still_skip = 1;
		counter++;
	}

	while (srv_message[counter] != '\r' || counter != BF_SIZE)
	{
		if (elem > 15 || ch > 512)
			break;
		if (srv_message[counter] == '!' && is_nickname)
		{
			while (srv_message[counter] != ' ')
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
		else
			res[elem][ch] = srv_message[counter];
		counter++;
		ch++;
	}
	return res;
}
