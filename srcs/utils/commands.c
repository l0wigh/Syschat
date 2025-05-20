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

	if (syschat->channel)
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

void commands_handle_clear()
{
	for (int i = 0; i != 1000; i++)
		if (write(1, "\n", 1) == -1)
			break;
}

void commands_handle_msg(t_syschat *syschat, char *command)
{
	while (command[0] != ' ')
		command++;
	*strchr(command, '\n') = '\0';
	strcat(command, "\r\n");
	send(syschat->net_socket, command, strlen(command), MSG_DONTWAIT);
}

void commands_handle_lock(t_syschat *syschat, char **parsed)
{
	if (syschat->channel)
		free(syschat->channel);
	syschat->channel = strdup(parsed[1]);
}

void commands_handle_part(t_syschat *syschat, char **parsed)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	if (strcmp(syschat->channel, parsed[1]) == 0)
	{
		free(syschat->channel);
		syschat->channel = NULL;
	}
	sprintf(message, "PART %s\r\n", parsed[1]);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
}

void commands_handle_cmd_list(void)
{
	write(1, "Commands list\n", 15);
	write(1, "-------------\n", 15);
	write(1, "exit                             -> Well... exit syschat\n", 58);
	write(1, "join #<channel>                  -> join the given channel\n", 60);
	write(1, "privmsg <username>               -> send a message to a user\n", 62);
	write(1, "nick <new nickname>              -> change your current nickname\n", 66);
	write(1, "clear                            -> clear the screen\n", 53);
	write(1, "msg #<chan>/<username> <message> -> send a message to a channel or to a user\n", 78);
	write(1, "lock #<channel>/<username>       -> lock yourself on a channel or private message\n", 83);
	write(1, "part #<channel>                  -> leave a channel\n", 53);
}

void commands_handle_ctcp(t_syschat *syschat, char **parsed)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	sprintf(message, "PRIVMSG %s :%c%s%c\r\n", parsed[1], 1, parsed[2], 1);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
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
		commands_handle_privmsg(syschat, parsed);
	else if (strcmp(parsed[0], "nick") == 0)
		commands_handle_nick(syschat, parsed);
	else if (strcmp(parsed[0], "clear") == 0)
		commands_handle_clear();
	else if (strcmp(parsed[0], "msg") == 0)
		commands_handle_msg(syschat, command);
	else if (strcmp(parsed[0], "lock") == 0)
		commands_handle_lock(syschat, parsed);
	else if (strcmp(parsed[0], "part") == 0)
		commands_handle_part(syschat, parsed);
	else if (strcmp(parsed[0], "?") == 0)
		commands_handle_cmd_list();
	else if (strcmp(parsed[0], "ctcp") == 0)
		commands_handle_ctcp(syschat, parsed);

	for (int i = 0; i != 16; i++)
		free(parsed[i]);
	free(parsed);
}

char **commands_parse(char *command)
{
	char **res;
	int counter = 1;
	int elem = 0;
	int ch = 0;
	int still_skip = 1;

	res = (char **) calloc(16, sizeof(char *));
	for (int i = 0; i != 16; i++)
		res[i] = (char *) calloc(BF_SIZE, sizeof(char));

	while (command[counter] != '\n')
	{
		if (elem > 15 || ch > 512)
			break;
		if (command[counter] == ' ' && still_skip)
		{
			elem++;
			ch = -1;
		}
		else if (command[counter] == ':' && still_skip)
		{
			still_skip = 0;
			ch = -1;
		}
		else
			res[elem][ch] = command[counter];
		counter++;
		ch++;
	}
	return res;
}
