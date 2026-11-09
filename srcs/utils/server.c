#include "server.h"
#include "buffers.h"
#include "ctcp.h"
#include <sys/socket.h>

static void	target_chan(char *target, size_t size, const char *value)
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
		/* The channel name is already the tab title: no need to repeat it. */
		snprintf(srv_message, BF_SIZE, "<\e[0;34m%s\e[0m>: \e[0;34m%s\e[0m",
			parsed[0], parsed[3]);
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
	if (parsed[2][0] != '#')
		target_chan(target, SYSCHAT_NAME_LEN, syschat->hostname);
	else
		target_chan(target, SYSCHAT_NAME_LEN, parsed[2]);
	snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] \e[0;32m%s\e[0m changed \e[0;35m%s\e[0m modes to \e[0;36m%s\e[0m",
		target, parsed[0], target, parsed[3]);
}

void server_handle_notice(t_syschat *syschat, char **parsed, char *srv_message, char *target)
{
	(void) syschat;
	bzero(srv_message, BF_SIZE);
	if (parsed[2][0] == '#')
		target_chan(target, SYSCHAT_NAME_LEN, parsed[2]);
	else if (parsed[2][0] == '*' || (syschat->nickname && strcasecmp(parsed[2], syschat->nickname) == 0))
		target_chan(target, SYSCHAT_NAME_LEN, syschat->hostname);
	else
		target_chan(target, SYSCHAT_NAME_LEN, parsed[0]);
	if (strcasecmp(target, syschat->nickname) == 0)
		target_chan(target, SYSCHAT_NAME_LEN, syschat->hostname);
	if (parsed[3][0] == 1)
		snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] CTCP answer: \e[0;36m%s\e[0m",
			parsed[0], parsed[3]);
	else
		snprintf(srv_message, BF_SIZE, "[\e[0;35m%s\e[0m] -\e[0;36m%s\e[0m- %s",
			target, parsed[0], parsed[3]);
}

#define LIST_BUFFER "Liste"

/*
** Returns -1 when the numeric is not part of a channel listing, 1 when the
** line must be displayed (in the Liste buffer) and 0 when it is swallowed.
** Checked before the generic routing: 322 names its channel in param 3, so
** otherwise a listed channel we already joined would land in its own tab.
*/
static int	numeric_listing(t_syschat *syschat, char **parsed, char *srv_message,
	char *target, int code)
{
	int	shown;

	if (!syschat->listing || (code != 321 && code != 322 && code != 323))
		return (-1);
	target_chan(target, SYSCHAT_NAME_LEN, LIST_BUFFER);
	if (code == 321)
		return (0);
	if (code == 323)
	{
		snprintf(srv_message, BF_SIZE, "\e[0;37m-- %d channel(s) listed\e[0m",
			syschat->list_received);
		syschat->listing = 0;
		return (1);
	}
	shown = 1;
	if (syschat->list_filter[0] && !strcasestr(parsed[3], syschat->list_filter))
		shown = 0;
	if (!shown)
		return (0);
	if (parsed[5][0])
		snprintf(srv_message, BF_SIZE, "\e[0;37m%-24s %4s  %s\e[0m", parsed[3], parsed[4], parsed[5]);
	else
		snprintf(srv_message, BF_SIZE, "\e[0;37m%-24s %4s\e[0m", parsed[3], parsed[4]);
	syschat->list_received++;
	return (1);
}

/*
** Returns 1 when `srv_message` holds something to display, 0 otherwise.
** `target` receives the buffer name the message belongs to.
*/
static int is_numeric(const char *token)
{
	if (!token || !token[0])
		return (0);
	for (int i = 0; token[i]; i++)
		if (token[i] < '0' || token[i] > '9')
			return (0);
	return (1);
}


/*
** Returns 1 when `srv_message` holds something to display, 0 otherwise.
** `target` receives the buffer name the message belongs to.
*/
int server_handle_message(t_syschat *syschat, char *srv_message, char *target)
{
	char		**parsed;
	int			displayed;
	char		raw[BF_SIZE * 2];
	FILE		*debug_file;

	displayed = 0;
	bzero(raw, sizeof(raw));
	snprintf(raw, sizeof(raw), "%s", srv_message);
	bzero(target, SYSCHAT_NAME_LEN);
	if (getenv("SYSCHAT_DEBUG")
		&& (debug_file = fopen(getenv("SYSCHAT_DEBUG"), "a")))
	{
		fprintf(debug_file, "[raw] %s\n", srv_message);
		fclose(debug_file);
	}
	parsed = server_parse_message(srv_message);
	bzero(srv_message, BF_SIZE);
	if (is_numeric(parsed[1]))
	{
		int	code;

		code = atoi(parsed[1]);
		if (code == 5)
		{
			/* ISUPPORT: several 005 lines per session, scan them all. */
			for (int i = 3; i != 16; i++)
				if (strncmp(parsed[i], "ELIST=", 6) == 0 && strchr(parsed[i], 'C'))
					syschat->elist_mask = 1;
		}
		displayed = numeric_listing(syschat, parsed, srv_message, target, code);
		if (displayed == -1)
		{
			const char	*target_name;
			const char	*text;
			int			text_index;

			displayed = 1;
			/* Only numerics that designate a buffer: errors about a target
			** and channel facts. Everything else stays in the server window. */
			target_name = NULL;
			switch (code)
			{
				case 353:
					target_name = parsed[4];
					break ;
				case 324: case 329: case 331: case 332: case 333: case 341:
				case 352: case 366: case 367: case 368:
				case 401: case 403: case 404: case 405: case 407: case 437:
				case 441: case 442: case 443: case 470: case 471: case 473:
				case 474: case 475: case 476: case 477: case 478: case 482:
				case 485:
					target_name = parsed[3];
					break ;
				default:
					break ;
			}
			target_chan(target, SYSCHAT_NAME_LEN, syschat->hostname);
			if (target_name && buffers_find(syschat, target_name) != -1)
				target_chan(target, SYSCHAT_NAME_LEN, target_name);
			text_index = 3;
			for (int i = 15; i >= 3; i--)
				if (parsed[i][0])
				{
					text_index = i;
					break ;
				}
			text = parsed[text_index];
			if (code == 353)
				snprintf(srv_message, BF_SIZE, "\e[0;37mUsers: %s\e[0m", text);
			else if (code == 332)
				snprintf(srv_message, BF_SIZE, "\e[0;37mTopic: %s\e[0m", text);
			else if (code == 324)
				snprintf(srv_message, BF_SIZE, "\e[0;37mModes: %s\e[0m", parsed[4]);
			else if (code == 329)
				snprintf(srv_message, BF_SIZE, "\e[0;37mCreated: %s\e[0m", parsed[4]);
			else if (code == 333)
			{
				char	setter[SYSCHAT_NAME_LEN];

				snprintf(setter, sizeof(setter), "%s", parsed[4]);
				setter[strcspn(setter, "!")] = '\0';
				snprintf(srv_message, BF_SIZE, "\e[0;37mTopic set by %s\e[0m", setter);
			}
			else
				snprintf(srv_message, BF_SIZE, "\e[0;37m%s\e[0m", text);
		}
		else if (displayed == 0)
			displayed = 0;
	}
	else if (strcmp(parsed[0], "PING") == 0 || strcmp(parsed[1], "PING") == 0)
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
	if (srv_message[0] != '\0' && strcmp(parsed[0], "PING") != 0
		&& strcmp(parsed[1], "PING") != 0)
		displayed = 1;
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
