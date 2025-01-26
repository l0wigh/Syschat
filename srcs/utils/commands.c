#include "commands.h"

void commands_handle_exit(t_syschat *syschat)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	sprintf(message, "PART %s\r\n", syschat->channel);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);

	bzero(message, BF_SIZE);
	sprintf(message, "QUIT %s :%s\r\n", syschat->channel, SYSCHAT_QUIT);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);

	error_cleaning(syschat);
	exit(0);
}

void commands_handle_join(t_syschat *syschat, char **parsed)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	if (strcmp(syschat->channel, "NIAC") != 0)
	{
		sprintf(message, "PART %s\r\n", syschat->channel);
		send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	}

	if (parsed[1][0] != '#')
	{
		char *tmp = (char *) calloc(BF_SIZE, sizeof(char));

		tmp[0] = '#';
		strcat(tmp, parsed[1]);
		free(parsed[1]);
		parsed[1] = strdup(tmp);
		free(tmp);
	}

	bzero(message, BF_SIZE);
	sprintf(message, "JOIN %s\r\n", parsed[1]);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);

	free(syschat->channel);
	syschat->channel = strdup(parsed[1]);
}

void commands_handle_privmsg(t_syschat *syschat, char **parsed)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	sprintf(message, "PRIVMSG %s :%s\r\n", parsed[1], parsed[2]);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
}

void commands_handle_nick(t_syschat *syschat, char **parsed)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	sprintf(message, "NICK %s\r\n", parsed[1]);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);

	free(syschat->nickname);
	syschat->nickname = strdup(parsed[1]);
}

void commands_handle_msg(t_syschat *syschat, char *command)
{
	while (command[0] != ' ')
		command++;
	*strchr(command, '\n') = '\0';
	strcat(command, "\r\n");
	send(syschat->net_socket, command, strlen(command), MSG_DONTWAIT);
}

void commands_execute(t_syschat *syschat, char *command)
{
	char **parsed;

	parsed = commands_parse(command);
	if (strcmp(parsed[0], "exit") == 0)
		commands_handle_exit(syschat);
	else if (strcmp(parsed[0], "join") == 0)
		commands_handle_join(syschat, parsed);
	else if (strcmp(parsed[0], "privmsg") == 0)
		commands_handle_join(syschat, parsed);
	else if (strcmp(parsed[0], "nick") == 0)
		commands_handle_nick(syschat, parsed);
	else if (strcmp(parsed[0], "clear") == 0)
	{
		for (int i = 0; i != 1000; i++)
			if (write(1, "\n", 1) == -1)
				break;
	}
	else if (strcmp(parsed[0], "msg") == 0)
		commands_handle_msg(syschat, command);

	for (int i = 0; i < 15; i++)
		free(parsed[i]);
	free(parsed);
}

char **commands_parse(char *command)
{
	char **res;
	int counter = 1;
	int elem = 0;
	int ch = 0;

	res = (char **) calloc(16, sizeof(char *));
	for (int i = 0; i <= 15; i++)
		res[i] = (char *) calloc(BF_SIZE, sizeof(char));

	while (command[counter] != '\n')
	{
		if (command[counter] == ' ')
		{
			elem++;
			ch = -1;
		}
		else
			if (ch != 512)
				res[elem][ch] = command[counter];
		counter++;
		ch++;
	}
	return res;
}
