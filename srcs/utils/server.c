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
		case 1:
			sprintf(srv_message, "[%s]$ %s just entered\n", syschat->channel, parsed[0]);
			break;
		case 2:
			sprintf(srv_message, "[%s] -> %s just entered\n", parsed[2], parsed[0]);
			break;
		case 3:
			sprintf(srv_message, "%s: %s just entered\n", parsed[2], parsed[0]);
			break;
		default:
			sprintf(srv_message, "[%s]: <%s> just entered\n", parsed[2], parsed[0]);
			break;
	}
}

void server_handle_quit(t_syschat *syschat, char **parsed, char *srv_message)
{
	bzero(srv_message, BF_SIZE);
	switch (SYSCHAT_PRINT_MODE)
	{
		case 1:
			sprintf(srv_message, "[%s]$ %s just left\n", syschat->channel, parsed[0]);
			break;
		case 2:
			sprintf(srv_message, "[#%s] -> %s just left\n", syschat->channel, parsed[0]);
			break;
		case 3:
			sprintf(srv_message, "%s: %s just left\n", syschat->channel, parsed[0]);
			break;
		default:
			sprintf(srv_message, "[#%s]: <%s> just left\n", syschat->channel, parsed[0]);
			break;
	}
}

void server_handle_privmsg(t_syschat *syschat, char **parsed, char *srv_message)
{
	bzero(srv_message, BF_SIZE);
	switch (SYSCHAT_PRINT_MODE)
	{
		case 1:
			sprintf(srv_message, "[%s@%s]$ %s\n", parsed[0], syschat->channel, parsed[3]);
			break;
		case 2:
			sprintf(srv_message, "[%s] %s -> %s\n", parsed[2], parsed[0], parsed[3]);
			break;
		case 3:
			sprintf(srv_message, "%s: %s\n", parsed[0], parsed[3]);
			break;
		default:
			sprintf(srv_message, "[%s] <%s>: %s\n", parsed[2], parsed[0], parsed[3]);
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
		server_handle_join(syschat, parsed, message);
	else if (strcmp(parsed[1], "QUIT") == 0 || strcmp(parsed[1], "PART") == 0)
		server_handle_quit(syschat, parsed, message);
	else if (strcmp(parsed[1], "PRIVMSG") == 0)
		server_handle_privmsg(syschat, parsed, srv_message);

	for (int i = 0; parsed[i]; i++)
		free(parsed[i]);
	free(parsed);
}

char **server_parse_message(char *serv_message)
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

	if (serv_message[0] == ':')
		counter++;

	while (serv_message[counter] != '\r')
	{
		if (serv_message[counter] == ':' && ch == 0)
		{
			counter++;
			still_skip = 0;
		}
		if ((serv_message[counter] == ' ' || (serv_message[counter] == '!' && is_nickname == 0)) && still_skip)
		{
			if (serv_message[counter] == '!')
				while(serv_message[counter] != ' ')
					counter++;
			if (elem != 15)
				elem++;
			ch = -1;
			is_nickname = 1;
		}
		else
			res[elem][ch] = serv_message[counter];
		counter++;
		ch++;
	}

	return res;
}
