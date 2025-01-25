#include "server.h"

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
	bzero(srv_message, BF_SIZE);
	switch (SYSCHAT_PRINT_MODE)
	{
		case SIMPLE:
			sprintf(srv_message, "\e[0;35m%s\e[0m: \e[0;32m%s\e[0m just \e[0;32mentered\e[0m\n", syschat->channel, parsed[0]);
			break;
		default:
			sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m just \e[0;32mentered\e[0m\n", syschat->channel, parsed[0]);
			break;
	}
}

void server_handle_quit(t_syschat *syschat, char **parsed, char *srv_message)
{
	bzero(srv_message, BF_SIZE);
	switch (SYSCHAT_PRINT_MODE)
	{
		case SIMPLE:
			sprintf(srv_message, "\e[0;35m%s\e[0m: \e[0;32m%s\e[0m just \e[0;31mleft\e[0m\n", syschat->channel, parsed[0]);
			break;
		default:
			sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m just \e[0;31mleft\e[0m\n", syschat->channel, parsed[0]);
			break;
	}
}

void server_handle_privmsg(char **parsed, char *srv_message)
{
	bzero(srv_message, BF_SIZE);
	switch (SYSCHAT_PRINT_MODE)
	{
		case SHELL:
			memmove(parsed[2], parsed[2]+1, strlen(parsed[2]));
			sprintf(srv_message, "[\e[0;32m%s\e[0m@\e[0;35m%s\e[0m]$ \e[0;34m%s\e[0m \n", parsed[0], parsed[2], parsed[3]);
			break;
		case ARROW:
			sprintf(srv_message, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m -> \e[0;34m%s\e[0m \n", parsed[2], parsed[0], parsed[3]);
			break;
		case SIMPLE:
			sprintf(srv_message, "\e[0;35m%s\e[0m: \e[0;34m%s\e[0m \n", parsed[0], parsed[3]);
			break;
		default:
			sprintf(srv_message, "[\e[0;35m%s\e[0m] <\e[0;32m%s\e[0m>: \e[0;34m%s\e[0m \n", parsed[2], parsed[0], parsed[3]);
			break;
	}
}

void server_handle_nick(t_syschat *syschat, char **parsed, char *srv_message)
{
	bzero(srv_message, BF_SIZE);
	switch (SYSCHAT_PRINT_MODE)
	{
		case 3:
			sprintf(srv_message, "%s: \e[0;31m%s\e[0m is now \e[0;32m%s\e[0m\n", syschat->channel, parsed[0], parsed[2]);
			break;
		default:
			sprintf(srv_message, "[%s] \e[0;31m%s\e[0m is now \e[0;32m%s\e[0m\n", syschat->channel, parsed[0], parsed[2]);
			break;
	}
}

void server_handle_message(t_syschat *syschat, char *srv_message)
{
	char **parsed;
	char message[BF_SIZE];

	parsed = server_parse_message(srv_message);

	if (strcmp(parsed[0], "PING") == 0)
		server_handle_ping(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "JOIN") == 0)
		server_handle_join(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "QUIT") == 0 || strcmp(parsed[1], "PART") == 0)
		server_handle_quit(syschat, parsed, srv_message);
	else if (strcmp(parsed[1], "PRIVMSG") == 0)
		server_handle_privmsg(parsed, srv_message);
	else if (strcmp(parsed[1], "NICK") == 0)
		server_handle_nick(syschat, parsed, srv_message);
	else
		if (SYSCHAT_QUIET)
			bzero(srv_message, BF_SIZE);
}

char **server_parse_message(char *srv_message)
{
	char **res;
	int counter = 0;
	int elem = 0;
	int ch = 0;
	int is_nickname = 0;
	int still_skip = 1;

	res = (char **) calloc(16, sizeof(char *));
	for (int i = 0; i != 16; i++)
		res[i] = (char *) calloc(BF_SIZE, sizeof(char));

	if (srv_message[0] == ':')
		counter++;

	while (srv_message[counter] != '\r')
	{
		if (srv_message[counter] == ':' && ch == 0)
		{
			counter++;
			still_skip = 0;
		}
		if ((srv_message[counter] == ' ' || (srv_message[counter] == '!' && is_nickname == 0)) && still_skip)
		{
			if (srv_message[counter] == '!')
				while(srv_message[counter] != ' ')
					counter++;
			if (elem != 15)
				elem++;
			ch = -1;
			is_nickname = 1;
		}
		else
			res[elem][ch] = srv_message[counter];
		counter++;
		ch++;
	}

	return res;
}
