#include "commands.h"
#include "buffers.h"
#include <time.h>

void commands_handle_exit(t_syschat *syschat)
{
	char	*name;
	char	message[BF_SIZE];

	name = buffers_active_name(syschat);
	if (name)
	{
		bzero(message, BF_SIZE);
		snprintf(message, BF_SIZE, "PART %s\r\n", name);
		send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	}
	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "QUIT :%s\r\n", SYSCHAT_QUIT);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);

	error_cleaning(syschat);
	exit(0);
}

void commands_handle_join(t_syschat *syschat, char **parsed)
{
	char	message[BF_SIZE];
	char	name[SYSCHAT_NAME_LEN];
	int		index;

	if (parsed[1][0] == '\0')
		return ;
	bzero(name, SYSCHAT_NAME_LEN);
	if (parsed[1][0] != '#')
		snprintf(name, SYSCHAT_NAME_LEN, "#%s", parsed[1]);
	else
		snprintf(name, SYSCHAT_NAME_LEN, "%s", parsed[1]);
	if (buffers_find(syschat, name) != -1)
	{
		index = buffers_find(syschat, name);
		buffers_switch(syschat, index);
		return ;
	}
	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "JOIN %s\r\n", name);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	index = buffers_join(syschat, name);
	if (index != -1)
		buffers_switch(syschat, index);
}

void commands_handle_part(t_syschat *syschat, char **parsed)
{
	char	*name;
	char	message[BF_SIZE];

	name = parsed[1][0] ? parsed[1] : buffers_active_name(syschat);
	if (!name)
		return ;
	if (name[0] != '#' && parsed[1][0] == '\0')
		return ;
	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "PART %s\r\n", name);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	buffers_leave(syschat, name);
}

void commands_handle_switch(t_syschat *syschat, char **parsed)
{
	int	index;

	if (parsed[1][0] == '\0')
		return ;
	index = buffers_find(syschat, parsed[1]);
	if (index == -1 && parsed[1][0] != '#')
	{
		char	name[SYSCHAT_NAME_LEN];

		snprintf(name, SYSCHAT_NAME_LEN, "#%s", parsed[1]);
		index = buffers_find(syschat, name);
	}
	if (index != -1)
		buffers_switch(syschat, index);
}

void commands_handle_cycle(t_syschat *syschat, char **parsed)
{
	int	step;
	int	count;

	step = parsed[0][0] == 'p' ? -1 : 1;
	count = 1;
	if (parsed[1][0] >= '0' && parsed[1][0] <= '9')
		count = atoi(parsed[1]);
	while (count-- > 0)
		buffers_cycle(syschat, step);
}

void commands_handle_privmsg(t_syschat *syschat, char **parsed)
{
	char	message[BF_SIZE];

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "PRIVMSG %s :%s\r\n", parsed[1], parsed[2]);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
}

void commands_handle_nick(t_syschat *syschat, char **parsed)
{
	char	message[BF_SIZE];

	if (parsed[1][0] == '\0')
		return ;
	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "NICK %s\r\n", parsed[1]);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	free(syschat->nickname);
	syschat->nickname = strdup(parsed[1]);
}

void commands_handle_clear(t_syschat *syschat)
{
	buffers_clear(syschat, syschat->active);
}

void commands_handle_msg(t_syschat *syschat, char *command)
{
	char	*target;
	char	*text;
	char	message[BF_SIZE];
	char	line[BF_SIZE * 2];
	size_t	len;
	int		index;

	target = strchr(command, ' ');
	if (!target)
		return ;
	target++;
	text = strchr(target, ' ');
	if (!text)
		return ;
	*text++ = '\0';
	if (*text == ':')
		text++;
	len = strlen(text);
	if (len && text[len - 1] == '\n')
		text[len - 1] = '\0';
	if (!*text)
		return ;
	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "PRIVMSG %s :%s\r\n", target, text);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	bzero(line, sizeof(line));
	if (target[0] == '#')
		snprintf(line, sizeof(line), "[\e[0;35m%s\e[0m] <\e[0;33m%s\e[0m>: \e[0;33m%s\e[0m",
			target, syschat->nickname, text);
	else
		snprintf(line, sizeof(line), "\e[0;33m%s\e[0m -> \e[0;34m%s\e[0m: \e[0;33m%s\e[0m",
			syschat->nickname, target, text);
	index = buffers_route(syschat, target);
	buffers_append(syschat, index, line);
	if (index != -1)
		buffers_switch(syschat, index);
}

#define LIST_BUFFER "Liste"

/*
** /list [motif] -- asks the server for its channel list into a "Liste" buffer.
** A server advertising ELIST=C filters itself; otherwise the mask is dropped
** and the filtering happens here, line by line.
*/
void commands_handle_list(t_syschat *syschat, char **parsed)
{
	char	message[BF_SIZE];
	char	mask[SYSCHAT_NAME_LEN];
	char	line[BF_SIZE];
	size_t	len;
	int		index;

	if (syschat->listing)
	{
		buffers_append(syschat, syschat->active,
			"\e[0;31m/list already in progress, wait for it to finish\e[0m");
		return ;
	}
	index = buffers_find(syschat, LIST_BUFFER);
	if (index == -1)
		index = buffers_new(syschat, LIST_BUFFER);
	if (index == -1)
		index = 0;
	buffers_clear(syschat, index);
	buffers_switch(syschat, index);
	bzero(mask, sizeof(mask));
	bzero(syschat->list_filter, sizeof(syschat->list_filter));
	if (parsed[1][0])
	{
		len = strlen(parsed[1]);
		while (len && parsed[1][len - 1] == '*')
			parsed[1][--len] = '\0';
		while (parsed[1][0] == '*')
			memmove(parsed[1], parsed[1] + 1, strlen(parsed[1]));
		if (syschat->elist_mask)
			snprintf(mask, sizeof(mask), "*%s*", parsed[1]);
		else
			snprintf(syschat->list_filter, sizeof(syschat->list_filter), "%s", parsed[1]);
	}
	syschat->listing = 1;
	syschat->listing_since = time(NULL);
	syschat->list_received = 0;
	if (mask[0])
		snprintf(message, BF_SIZE, "LIST %s\r\n", mask);
	else
		snprintf(message, BF_SIZE, "LIST\r\n");
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	bzero(line, sizeof(line));
	if (mask[0])
		snprintf(line, sizeof(line), "\e[0;37mListing %s ...\e[0m", mask);
	else
		snprintf(line, sizeof(line), "\e[0;37mListing channels ...\e[0m");
	buffers_append(syschat, index, line);
}

void commands_handle_cmd_list(t_syschat *syschat)
{
	static const char *help[] = {
		"Commands list",
		"-------------",
		"exit                             -> Well... exit syschat",
		"join #<channel>                  -> join the given channel",
		"part [#<channel>]                -> leave a channel",
		"close [#<channel>]               -> same as part",
		"next [n] / prev [n]              -> go to next / previous buffer",
		"list [motif]                     -> list the server channels in a buffer",
		"switch #<channel>                -> show the given buffer",
		"privmsg <username>               -> send a message to a user",
		"nick <new nickname>              -> change your current nickname",
		"clear                            -> clear the current buffer",
		"msg <target> <message>           -> send a message to a channel or a user",
		"lock <name>                      -> same as switch (kept for compat)",
		"?                                -> this list",
		"Tab / Shift-Tab                  -> next / previous buffer",
		NULL
	};

	for (int i = 0; help[i]; i++)
		buffers_append(syschat, syschat->active, help[i]);
}

void commands_handle_ctcp(t_syschat *syschat, char **parsed)
{
	char	message[BF_SIZE];

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "PRIVMSG %s :%c%s%c\r\n", parsed[1], 1, parsed[2], 1);
	send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
}

void commands_execute(t_syschat *syschat, char *command)
{
	char **parsed;

	parsed = commands_parse(command);
	if (strcmp(parsed[0], "exit") == 0 || strcmp(parsed[0], "quit") == 0)
		commands_handle_exit(syschat);
	else if (strcmp(parsed[0], "join") == 0)
		commands_handle_join(syschat, parsed);
	else if (strcmp(parsed[0], "privmsg") == 0)
		commands_handle_privmsg(syschat, parsed);
	else if (strcmp(parsed[0], "nick") == 0)
		commands_handle_nick(syschat, parsed);
	else if (strcmp(parsed[0], "clear") == 0)
		commands_handle_clear(syschat);
	else if (strcmp(parsed[0], "msg") == 0)
		commands_handle_msg(syschat, command);
	else if (strcmp(parsed[0], "lock") == 0 || strcmp(parsed[0], "switch") == 0
		|| strcmp(parsed[0], "buffer") == 0)
		commands_handle_switch(syschat, parsed);
	else if (strcmp(parsed[0], "next") == 0 || strcmp(parsed[0], "prev") == 0)
		commands_handle_cycle(syschat, parsed);
	else if (strcmp(parsed[0], "part") == 0 || strcmp(parsed[0], "leave") == 0
		|| strcmp(parsed[0], "close") == 0)
		commands_handle_part(syschat, parsed);
	else if (strcmp(parsed[0], "list") == 0)
		commands_handle_list(syschat, parsed);
	else if (strcmp(parsed[0], "?") == 0)
		commands_handle_cmd_list(syschat);
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

	while (command[counter] != '\n' && command[counter] != '\0')
	{
		if (elem > 15 || ch > 511)
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
		else if (ch >= 0 && ch < BF_SIZE - 1)
			res[elem][ch] = command[counter];
		counter++;
		ch++;
	}
	return res;
}
