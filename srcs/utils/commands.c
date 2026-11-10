#include "commands.h"
#include "network.h"
#include "tls.h"
#include "buffers.h"
#include "screen.h"
#include <time.h>

#define IRC_MAX_LINE 512

/*
** Single exit point for a PRIVMSG: truncates to the server limit (512 bytes
** including CRLF and the prefix) on a complete UTF-8 character, and tells the
** caller when it had to cut. Returns the text actually sent, or NULL.
*/
const char *send_privmsg(t_syschat *syschat, const char *target, const char *text, int *truncated)
{
	static char	payload[BF_SIZE * 2];
	char		message[IRC_MAX_LINE + SYSCHAT_NAME_LEN + 16];
	size_t		room;
	size_t		len;

	if (truncated)
		*truncated = 0;
	room = IRC_MAX_LINE - 2 - 8 - 1 - strlen(target) - 1;
	len = strlen(text);
	if (len > room)
	{
		len = room;
		while (len > 0 && ((unsigned char) text[len] & 0xC0) == 0x80)
			len--;
		if (truncated)
			*truncated = 1;
	}
	if (len == 0)
		return (NULL);
	memcpy(payload, text, len);
	payload[len] = '\0';
	snprintf(message, sizeof(message), "PRIVMSG %s :%s\r\n", target, payload);
	net_send(SYSC(syschat), message, strlen(message));
	return (payload);
}

/*
** Single exit point for a raw IRC line: the caller passes the command and its
** whole parameter list without CRLF ("WHOIS nick", "MODE #chan +o bob"), the
** helper enforces the 512-byte server limit on a complete UTF-8 character and
** sets *truncated when it had to cut. Returns the line actually sent (static
** buffer, valid until the next call), or NULL when nothing could be sent.
*/
const char *send_raw(t_syschat *syschat, const char *line, int *truncated)
{
	static char	payload[IRC_MAX_LINE + 1];
	char		message[IRC_MAX_LINE + 2];
	size_t		room;
	size_t		len;

	if (truncated)
		*truncated = 0;
	if (!line)
		return (NULL);
	room = IRC_MAX_LINE - 2;
	len = strlen(line);
	if (len > room)
	{
		len = room;
		while (len > 0 && ((unsigned char) line[len] & 0xC0) == 0x80)
			len--;
		if (truncated)
			*truncated = 1;
	}
	if (len == 0)
		return (NULL);
	memcpy(payload, line, len);
	payload[len] = '\0';
	snprintf(message, sizeof(message), "%s\r\n", payload);
	net_send(SYSC(syschat), message, len + 2);
	return (payload);
}

void commands_handle_exit(t_syschat *syschat)
{
	char	*name;
	char	message[BF_SIZE];
	int		kind;

	kind = buffers_active_kind(syschat);
	name = buffers_active_name(syschat);
	if (name && (kind == BUF_CHANNEL || kind == BUF_PRIVATE))
	{
		bzero(message, BF_SIZE);
		snprintf(message, BF_SIZE, "PART %s\r\n", name);
		net_send(SYSC(syschat), message, strlen(message));
	}
	/*
	** One QUIT per connection: the second server used to see nothing but a
	** TCP drop when the client exited.
	*/
	for (int i = 0; i != syschat->conn_count; i++)
	{
		if (!syschat->conns[i].alive || syschat->conns[i].fd < 0)
			continue ;
		bzero(message, BF_SIZE);
		snprintf(message, BF_SIZE, "QUIT :%s\r\n", SYSCHAT_QUIT);
		net_send(&syschat->conns[i], message, strlen(message));
	}

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
	net_send(SYSC(syschat), message, strlen(message));
	index = buffers_join(syschat, name);
	if (index != -1)
		buffers_switch(syschat, index);
}

/*
** Multi-server addressing, frozen contract:
**   <label>          a server (names the connection, its console window)
**   <label>/<buffer> a buffer on another server
**   <buffer>         the buffer of the current server first, never an
**                    implicit jump to another server's tab.
*/
static int	find_server(t_syschat *syschat, const char *label)
{
	int	i;

	if (!label || !label[0])
		return (-1);
	for (i = 0; i != syschat->conn_count; i++)
		if (syschat->conns[i].alive && syschat->conns[i].label
			&& strcasecmp(syschat->conns[i].label, label) == 0)
			return (i);
	return (-1);
}

static int	split_addr(t_syschat *syschat, const char *word, int *sid,
	char *name, size_t name_size)
{
	char	buf[SYSCHAT_NAME_LEN * 2];
	char	*slash;
	int		id;

	if (!word || !word[0] || strlen(word) >= sizeof(buf))
		return (0);
	snprintf(buf, sizeof(buf), "%s", word);
	slash = strchr(buf, '/');
	if (!slash)
		return (0);
	*slash = '\0';
	id = find_server(syschat, buf);
	if (id < 0 || slash[1] == '\0')
		return (0);
	*sid = id;
	snprintf(name, name_size, "%s", slash + 1);
	return (1);
}

static void	commands_say(t_syschat *syschat, const char *text)
{
	int	index;

	/*
	** Started with no argument: there is no buffer yet, so a command that
	** has something to say (a failed /connect, a bad label) writes on the
	** terminal directly instead of vanishing.
	*/
	if (syschat->buffer_count == 0)
	{
		/* Painted by screen_draw() in the empty frame; stdout would be
		** wiped by the repaint that follows every command. */
		syschat_status(syschat, text);
		return ;
	}
	index = syschat->active;
	if (index < 0 || index >= syschat->buffer_count)
		index = 0;
	buffers_append(syschat, index, text);
}

void commands_handle_part(t_syschat *syschat, char **parsed)
{
	char	name[SYSCHAT_NAME_LEN];
	char	message[BF_SIZE];
	int		sid;
	int		index;
	int		kind;

	sid = syschat->current;
	index = syschat->active;
	kind = buffers_active_kind(syschat);
	if (parsed[1][0] == '\0')
	{
		if (index < 0)
			return ;
		sid = (int) syschat->buffers[index].server_id;
		snprintf(name, sizeof(name), "%s", syschat->buffers[index].name);
	}
	else if (split_addr(syschat, parsed[1], &sid, name, sizeof(name)))
	{
		index = buffers_find_on(syschat, (unsigned) sid, name);
		if (index == -1)
			return ;
		kind = syschat->buffers[index].kind;
	}
	else
	{
		int	srv;

		/* close <serveur>: the whole connection goes, buffers included */
		srv = find_server(syschat, parsed[1]);
		if (srv >= 0)
		{
			syschat_conn_close(syschat, srv);
			if (!syschat_any_alive(syschat))
				syschat->running = 0;
			return ;
		}
		index = buffers_find(syschat, parsed[1]);
		if (index == -1)
			return ;
		kind = syschat->buffers[index].kind;
		sid = (int) syschat->buffers[index].server_id;
		snprintf(name, sizeof(name), "%s", syschat->buffers[index].name);
	}
	if (kind == BUF_SERVER)
	{
		syschat_conn_close(syschat, sid);
		if (!syschat_any_alive(syschat))
			syschat->running = 0;
		return ;
	}
	if (kind == BUF_CHANNEL)
	{
		bzero(message, BF_SIZE);
		snprintf(message, BF_SIZE, "PART %s\r\n", name);
		net_send(&syschat->conns[sid], message, strlen(message));
	}
	buffers_leave_on(syschat, (unsigned) sid, name);
}

void commands_handle_switch(t_syschat *syschat, char **parsed)
{
	char	name[SYSCHAT_NAME_LEN];
	char	line[BF_SIZE];
	char	*pos;
	size_t	used;
	int		sid;
	int		index;
	int		candidates;

	if (parsed[1][0] == '\0')
		return ;
	if (split_addr(syschat, parsed[1], &sid, name, sizeof(name)))
	{
		index = buffers_find_on(syschat, (unsigned) sid, name);
		if (index != -1)
		{
			buffers_switch(syschat, index);
			return ;
		}
		snprintf(line, sizeof(line), "no such buffer: %s", parsed[1]);
		commands_say(syschat, line);
		return ;
	}
	snprintf(name, sizeof(name), "%s", parsed[1]);
	index = buffers_find(syschat, name);
	if (index == -1 && name[0] != '#')
	{
		snprintf(name, sizeof(name), "#%s", parsed[1]);
		index = buffers_find(syschat, name);
	}
	if (index != -1)
	{
		buffers_switch(syschat, index);
		return ;
	}
	/*
	** No implicit cross-server jump: the same "#chan" on two servers is a
	** real ambiguity, and guessing would silently move the user's eyes.
	** Stable prefix, one line, candidates sorted by server slot.
	*/
	used = snprintf(line, sizeof(line), "ambiguous buffer:");
	candidates = 0;
	for (int i = 0; i != syschat->conn_count; i++)
	{
		if (!syschat->conns[i].alive || !syschat->conns[i].label)
			continue ;
		if (buffers_find_on(syschat, (unsigned) i, name) == -1)
			continue ;
		pos = line + used;
		used += snprintf(pos, sizeof(line) - used, " %s/%s",
			syschat->conns[i].label, name);
		candidates++;
		if (used >= sizeof(line) - 1)
			break ;
	}
	if (candidates)
	{
		commands_say(syschat, line);
		return ;
	}
	snprintf(line, sizeof(line), "no such buffer: %s", parsed[1]);
	commands_say(syschat, line);
}

/*
** /connect <nickname> <server> <host> [port]: the nickname is per
** connection, the server name labels it, and the TLS policy is the same as
** on the command line (port 6697, or SYSCHAT_TLS, or the flags). No port
** means 6697, so a fresh /connect tries TLS first.
*/
void commands_handle_connect(t_syschat *syschat, char **parsed)
{
	char	line[BF_SIZE];
	char	*end;
	long	value;
	int		port;
	int		id;
	int		buf_index;

	if (parsed[1][0] == '\0' || parsed[2][0] == '\0' || parsed[3][0] == '\0')
	{
		commands_say(syschat, "usage: /connect <nickname> <server> <host> [port]");
		return ;
	}
	if (find_server(syschat, parsed[2]) >= 0)
	{
		commands_say(syschat, "connect: this server name is already used");
		return ;
	}
	/* No port means the TLS port: /connect asks for encryption by default. */
	port = 6697;
	if (parsed[4][0])
	{
		end = NULL;
		value = strtol(parsed[4], &end, 10);
		if (end == parsed[4] || *end != '\0' || value < 1 || value > 65535)
		{
			commands_say(syschat, "connect: port must be an integer between 1 and 65535");
			return ;
		}
		port = (int) value;
	}
	id = syschat_conn_add(syschat, parsed[2], parsed[3], parsed[1], port);
	if (id < 0)
	{
		commands_say(syschat, "connect: no free server slot");
		return ;
	}
	if (network_connect_server(syschat, &syschat->conns[id], 0) == -1)
	{
		int	strict = syschat->conns[id].tls_required;
		char	reason[256];

		snprintf(reason, sizeof(reason), "%s", syschat->conns[id].tls_reason);
		syschat_conn_close(syschat, id);
		/* Only claim a TLS failure when TLS was actually attempted: a dead
		** host fails before any handshake. */
		if (strict && reason[0])
			snprintf(line, sizeof(line),
				"connect: TLS is required (--tls-required) and the handshake failed: %s",
				reason);
		else
			snprintf(line, sizeof(line), "connect: cannot reach %s:%d", parsed[3], port);
		commands_say(syschat, line);
		return ;
	}
	syschat_say_hello(&syschat->conns[id]);
	buf_index = buffers_new_on(syschat, (unsigned) id, syschat->conns[id].label);
	if (buf_index != -1)
		buffers_switch(syschat, buf_index);
	/*
	** A handshake that failed falls back to plaintext, and the warning
	** written by network_connect_server() could not land anywhere: this
	** connection had no buffer yet, and `active` may be another server's
	** tab. Say it here, once the window exists.
	*/
	if (syschat->conns[id].tls && !syschat->conns[id].tls_active)
	{
		snprintf(line, sizeof(line),
			"\e[0;31mTLS: handshake failed (%s) -- falling back to plaintext, "
			"this session is NOT encrypted\e[0m",
			syschat->conns[id].tls_reason[0] ? syschat->conns[id].tls_reason : "no answer");
		buffers_notice(syschat, (unsigned) id, line);
	}
	snprintf(line, sizeof(line), "-- connected to %s as %s (%s:%d, %s)", parsed[2],
		parsed[1], parsed[3], port,
		syschat->conns[id].tls_active ? "TLS" : "plaintext");
	commands_say(syschat, line);
}

void commands_handle_disconnect(t_syschat *syschat, char **parsed)
{
	char	message[BF_SIZE];
	char	label[SYSCHAT_NAME_LEN];
	int		id;

	if (parsed[1][0])
		id = find_server(syschat, parsed[1]);
	else
		id = syschat->current;
	if (id < 0 || !syschat->conns[id].alive)
	{
		commands_say(syschat, "disconnect: no such server");
		return ;
	}
	snprintf(label, sizeof(label), "%s",
		syschat->conns[id].label ? syschat->conns[id].label : "server");
	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "QUIT :%s\r\n", SYSCHAT_QUIT);
	net_send(&syschat->conns[id], message, strlen(message));
	syschat_conn_close(syschat, id);
	if (!syschat_any_alive(syschat))
		syschat->running = 0;
	/* Say it in a window that still exists; the caller repaints. */
	snprintf(message, BF_SIZE, "\e[0;31m-- %s disconnected\e[0m", label);
	buffers_notice(syschat, (unsigned) id, message);
}

/*
** /server alone lists the connections; /server <label> shows that server's
** own window (it does not connect: /connect does).
*/
void commands_handle_server(t_syschat *syschat, char **parsed)
{
	char	line[BF_SIZE];
	char	info[128];
	int		id;
	int		index;

	if (parsed[1][0] == '\0')
	{
		for (int i = 0; i != syschat->conn_count; i++)
		{
			if (!syschat->conns[i].alive)
				continue ;
			tls_summary(&syschat->conns[i], info, sizeof(info));
			snprintf(line, sizeof(line), "%s%s: %s:%d (%s)",
				i == syschat->current ? "* " : "  ",
				syschat->conns[i].label ? syschat->conns[i].label : "?",
				syschat->conns[i].hostname ? syschat->conns[i].hostname : "?",
				syschat->conns[i].port, info);
			commands_say(syschat, line);
		}
		return ;
	}
	id = find_server(syschat, parsed[1]);
	if (id < 0)
	{
		commands_say(syschat, "server: no such server");
		return ;
	}
	index = buffers_find_on(syschat, (unsigned) id, syschat->conns[id].label);
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
	net_send(SYSC(syschat), message, strlen(message));
}

void commands_handle_nick(t_syschat *syschat, char **parsed)
{
	char	message[BF_SIZE];

	if (parsed[1][0] == '\0')
		return ;
	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "NICK %s\r\n", parsed[1]);
	net_send(SYSC(syschat), message, strlen(message));
	free(SYSC(syschat)->nickname);
	SYSC(syschat)->nickname = strdup(parsed[1]);
}

void commands_handle_clear(t_syschat *syschat)
{
	buffers_clear(syschat, syschat->active);
}

/*
** /whois <pseudo> -- the answer comes back as four separate numerics (311,
** 319, 330, 671) closed by 318. None of them repeats the requester, so the
** target is remembered on the connection: server.c needs it to pick the
** window, and 318/401 clear it.
*/
void commands_handle_whois(t_syschat *syschat, char **parsed)
{
	char	line[BF_SIZE];

	if (parsed[1][0] == '\0')
	{
		commands_say(syschat, "usage : /whois <pseudo>");
		return ;
	}
	bzero(line, sizeof(line));
	snprintf(line, sizeof(line), "WHOIS %s", parsed[1]);
	if (!send_raw(syschat, line, NULL))
		return ;
	snprintf(SYSC(syschat)->whois_nick, SYSCHAT_NAME_LEN, "%s", parsed[1]);
}

/*
** Everything after the `skip` first words of a raw command line, trailing
** CR/LF removed. commands_parse() splits on single spaces and stops at 16
** parameters: the text of a /topic or the reason of a /kick must keep its own
** spelling, so it is read back from the raw line, like commands_handle_msg().
*/
static char	*commands_rest(char *command, int skip)
{
	char	*p;
	int		word;

	p = command;
	if (*p == '/')
		p++;
	for (word = 0; word < skip; word++)
	{
		while (*p && *p != ' ')
			p++;
		while (*p == ' ')
			p++;
	}
	p[strcspn(p, "\r\n")] = '\0';
	return (p);
}

/*
** The channel the user is looking at, or NULL when the active tab is not one.
** Commands that default to "the current channel" must say so instead of
** guessing a channel name.
*/
static const char	*commands_active_channel(t_syschat *syschat)
{
	if (buffers_active_kind(syschat) != BUF_CHANNEL)
		return (NULL);
	return (buffers_active_name(syschat));
}

/*
** /names [canal] -- 353 (the list) and 366 (its end) are already routed to the
** channel tab by server.c: nothing to add on the receive side.
*/
void commands_handle_names(t_syschat *syschat, char **parsed)
{
	char	line[BF_SIZE];

	if (parsed[1][0])
		snprintf(line, sizeof(line), "NAMES %s", parsed[1]);
	else
		snprintf(line, sizeof(line), "NAMES");
	send_raw(syschat, line, NULL);
}

/*
** /topic                -> TOPIC on the current channel (read it)
** /topic #canal         -> read that channel's topic
** /topic <sujet>        -> set the topic of the current channel
** /topic #canal <sujet> -> set that channel's topic
** 331/332/333 already reach the channel tab.
*/
void commands_handle_topic(t_syschat *syschat, char *command, char **parsed)
{
	char	line[BF_SIZE];
	char	canal[SYSCHAT_NAME_LEN];
	char	*arg;
	char	*text;
	const char	*active;

	(void) parsed;
	active = commands_active_channel(syschat);
	arg = commands_rest(command, 1);
	bzero(canal, sizeof(canal));
	text = NULL;
	if (*arg == '#')
	{
		snprintf(canal, sizeof(canal), "%s", arg);
		text = strchr(canal, ' ');
		if (text)
			*text++ = '\0';
	}
	else if (*arg)
		text = arg;
	if (!canal[0])
	{
		if (!active)
		{
			commands_say(syschat, "usage : /topic [#canal] [sujet]");
			return ;
		}
		snprintf(canal, sizeof(canal), "%s", active);
	}
	if (text && *text)
		snprintf(line, sizeof(line), "TOPIC %s :%s", canal, text);
	else
		snprintf(line, sizeof(line), "TOPIC %s", canal);
	send_raw(syschat, line, NULL);
}

/*
** /mode [cible] [modes] -- without arguments it asks for our own modes
** (RPL_UMODEIS 221); with a target, the parameters are passed through as
** typed so "+o bob" stays "+o bob". MODE on the receive side already exists.
*/
void commands_handle_mode(t_syschat *syschat, char *command, char **parsed)
{
	char	line[BF_SIZE];
	char	*rest;

	(void) parsed;
	rest = commands_rest(command, 1);
	if (*rest)
		snprintf(line, sizeof(line), "MODE %s", rest);
	else if (SYSC(syschat)->nickname)
		snprintf(line, sizeof(line), "MODE %s", SYSC(syschat)->nickname);
	else
		return ;
	send_raw(syschat, line, NULL);
}

/*
** /kick [#canal] <pseudo> [raison] -- the channel defaults to the current one,
** and the reason keeps its spaces. KICK is already parsed on the receive side.
*/
void commands_handle_kick(t_syschat *syschat, char *command, char **parsed)
{
	char		line[BF_SIZE];
	char		canal[SYSCHAT_NAME_LEN];
	char		*reason;
	const char	*active;
	const char	*nick;
	int			skip;

	active = commands_active_channel(syschat);
	skip = 2;
	if (parsed[1][0] == '#')
	{
		snprintf(canal, sizeof(canal), "%s", parsed[1]);
		nick = parsed[2];
		skip = 3;
	}
	else
	{
		if (!active)
		{
			commands_say(syschat, "usage : /kick [#canal] <pseudo> [raison]");
			return ;
		}
		snprintf(canal, sizeof(canal), "%s", active);
		nick = parsed[1];
	}
	if (nick[0] == '\0')
	{
		commands_say(syschat, "usage : /kick [#canal] <pseudo> [raison]");
		return ;
	}
	reason = commands_rest(command, skip);
	if (*reason)
		snprintf(line, sizeof(line), "KICK %s %s :%s", canal, nick, reason);
	else
		snprintf(line, sizeof(line), "KICK %s %s", canal, nick);
	send_raw(syschat, line, NULL);
}

/*
** /me <texte> -- CTCP ACTION to the tab being looked at.
**
** The text is read back from the raw line, not from parsed[]: commands_parse()
** stops at 16 parameters and collapses repeated spaces, and an action keeps
** its own spelling. Both \001 delimiters are part of the budget: a truncated
** ACTION stays a well-formed CTCP instead of losing its closing \001.
*/
void commands_handle_me(t_syschat *syschat, char *command)
{
	char		line[BF_SIZE * 2];
	char		echo[BF_SIZE * 2];
	char		*text;
	const char	*target;
	size_t		room;
	size_t		len;
	int			kind;

	kind = buffers_active_kind(syschat);
	target = buffers_active_name(syschat);
	if (!target || (kind != BUF_CHANNEL && kind != BUF_PRIVATE))
	{
		commands_say(syschat, "usage : /me <texte> (dans un canal ou une conversation)");
		return ;
	}
	text = commands_rest(command, 1);
	if (!*text)
	{
		commands_say(syschat, "usage : /me <texte>");
		return ;
	}
	/* 512 - CRLF(2) - "PRIVMSG "(8) - ' '(1) - ':'(1) - target - "\001ACTION "(8) - closing \001 */
	room = IRC_MAX_LINE - 2 - 8 - 1 - 1 - strlen(target) - 8 - 1;
	len = strlen(text);
	if (len > room)
	{
		len = room;
		while (len > 0 && ((unsigned char) text[len] & 0xC0) == 0x80)
			len--;
	}
	snprintf(line, sizeof(line), "PRIVMSG %s :%cACTION %.*s%c",
		target, 1, (int) len, text, 1);
	if (!send_raw(syschat, line, NULL))
		return ;
	bzero(echo, sizeof(echo));
	snprintf(echo, sizeof(echo), "\e[0;36m* %s%s\e[0m %.*s",
		nick_color(SYSC(syschat)->nickname), SYSC(syschat)->nickname,
		(int) len, text);
	buffers_append(syschat, buffers_route(syschat, target), echo);
}

/*
** /notice <cible> <texte> -- NOTICE, the only message that must never trigger
** an automatic answer (an away reply to a notice is how mail loops start).
** The text is read back from the raw line, like /msg. Nothing is echoed
** locally: the server sends our own notice back only if it accepts it.
*/
void commands_handle_notice(t_syschat *syschat, char *command)
{
	char	line[BF_SIZE * 2];
	char	*arg;
	char	*text;

	arg = commands_rest(command, 1);
	text = strchr(arg, ' ');
	if (!text || !*arg)
	{
		commands_say(syschat, "usage : /notice <pseudo|canal> <texte>");
		return ;
	}
	*text++ = '\0';
	if (*text == ':')
		text++;
	if (!*text)
	{
		commands_say(syschat, "usage : /notice <pseudo|canal> <texte>");
		return ;
	}
	snprintf(line, sizeof(line), "NOTICE %s :%s", arg, text);
	send_raw(syschat, line, NULL);
}

/*
** /quote <ligne IRC> -- sends the line as typed, for what the client does not
** wrap yet (a service command, a server-specific MODE, an op command...).
** Nothing is added and nothing is validated: the server answers.
*/
void commands_handle_quote(t_syschat *syschat, char *command)
{
	char	*line;
	int		truncated;

	line = commands_rest(command, 1);
	if (!*line)
	{
		commands_say(syschat, "usage : /quote <ligne IRC>");
		return ;
	}
	if (!send_raw(syschat, line, &truncated))
		return ;
	if (truncated)
		commands_say(syschat, "\e[0;31mligne tronquée à la limite serveur (512 octets)\e[0m");
}

/*
** /away [texte] -- /away alone marks us back. The acknowledgement is not sent
** by us: 305 (RPL_UNAWAY) and 306 (RPL_NOWAWAY) come from the server and land
** in its window through the generic formatter.
*/
void commands_handle_away(t_syschat *syschat, char *command)
{
	char	line[BF_SIZE * 2];
	char	*text;

	text = commands_rest(command, 1);
	if (*text)
		snprintf(line, sizeof(line), "AWAY :%s", text);
	else
		snprintf(line, sizeof(line), "AWAY");
	send_raw(syschat, line, NULL);
}

/*
** /invite <pseudo> [#canal] -- the channel defaults to the one being looked
** at; RPL_INVITING (341) is already routed to the channel tab.
*/
void commands_handle_invite(t_syschat *syschat, char **parsed)
{
	char		line[BF_SIZE];
	const char	*nick;
	const char	*canal;

	if (!parsed[1][0])
	{
		commands_say(syschat, "usage : /invite <pseudo> [#canal]");
		return ;
	}
	nick = parsed[1];
	canal = parsed[2][0] ? parsed[2] : commands_active_channel(syschat);
	if (canal)
		snprintf(line, sizeof(line), "INVITE %s %s", nick, canal);
	else
		snprintf(line, sizeof(line), "INVITE %s", nick);
	send_raw(syschat, line, NULL);
}

/*
** /ignore                 -> list the masks
** /ignore <nick|nick*>    -> stop showing this nick
** /ignore -<nick|nick*>   -> show it again
** Nothing is sent to the server: the messages still arrive and are dropped in
** server_handle_privmsg()/server_handle_notice(), where the nick is known.
*/
void commands_handle_ignore(t_syschat *syschat, char **parsed)
{
	char		mask[SYSCHAT_NAME_LEN];
	char		line[BF_SIZE];
	const char	*arg;
	int			remove;

	if (!parsed[1][0])
	{
		if (syschat->ignore_count == 0)
			commands_say(syschat, "ignore: aucun masque");
		for (int i = 0; i != syschat->ignore_count; i++)
		{
			snprintf(line, sizeof(line), "ignore: %s", syschat->ignore[i]);
			commands_say(syschat, line);
		}
		return ;
	}
	arg = parsed[1];
	remove = (arg[0] == '-');
	if (remove)
		arg++;
	if (!arg[0])
	{
		commands_say(syschat, "usage : /ignore <pseudo|pseudo*> | /ignore -<pseudo>");
		return ;
	}
	snprintf(mask, sizeof(mask), "%s", arg);
	for (int i = 0; i != syschat->ignore_count; i++)
	{
		if (strcasecmp(syschat->ignore[i], mask) != 0)
			continue ;
		if (remove)
		{
			for (int j = i; j + 1 < syschat->ignore_count; j++)
				snprintf(syschat->ignore[j], sizeof(syschat->ignore[j]), "%s",
					syschat->ignore[j + 1]);
			syschat->ignore_count--;
		}
		snprintf(line, sizeof(line), "ignore: %s %s", remove ? "levé pour" : "déjà actif pour", mask);
		commands_say(syschat, line);
		return ;
	}
	if (remove)
	{
		snprintf(line, sizeof(line), "ignore: %s n'est pas dans la liste", mask);
		commands_say(syschat, line);
		return ;
	}
	if (syschat->ignore_count >= SYSCHAT_MAX_IGNORES)
	{
		commands_say(syschat, "ignore: liste pleine, retire un masque avec /ignore -<pseudo>");
		return ;
	}
	snprintf(syschat->ignore[syschat->ignore_count], SYSCHAT_NAME_LEN, "%s", mask);
	syschat->ignore_count++;
	snprintf(line, sizeof(line), "ignore: %s masqué", mask);
	commands_say(syschat, line);
}

void commands_handle_msg(t_syschat *syschat, char *command)
{
	char	*target;
	char	*text;
	char		line[BF_SIZE * 2];
	size_t		len;
	int			index;
	int			truncated;
	const char	*sent;

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
	sent = send_privmsg(syschat, target, text, &truncated);
	if (!sent)
		return ;
	bzero(line, sizeof(line));
	/* Same format as a channel message: the tab title is the target. */
	snprintf(line, sizeof(line), "<%s%s\e[0m> %s",
		nick_color(SYSC(syschat)->nickname), SYSC(syschat)->nickname, sent);
	index = buffers_route(syschat, target);
	buffers_append(syschat, index, line);
	if (truncated)
	{
		bzero(line, sizeof(line));
		snprintf(line, sizeof(line), "\e[0;31mmessage tronqué à %d/%d caractères (limite serveur 512 octets)\e[0m",
			screen_text_width(sent), screen_text_width(text));
		buffers_append(syschat, index, line);
	}
	if (index != -1)
		buffers_switch(syschat, index);
}

#define LIST_BUFFER SYSCHAT_LIST_BUFFER

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

	if (SYSC(syschat)->listing)
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
	bzero(SYSC(syschat)->list_filter, sizeof(SYSC(syschat)->list_filter));
	if (parsed[1][0])
	{
		len = strlen(parsed[1]);
		while (len && parsed[1][len - 1] == '*')
			parsed[1][--len] = '\0';
		while (parsed[1][0] == '*')
			memmove(parsed[1], parsed[1] + 1, strlen(parsed[1]));
		if (SYSC(syschat)->elist_mask)
			snprintf(mask, sizeof(mask), "*%s*", parsed[1]);
		else
			snprintf(SYSC(syschat)->list_filter, sizeof(SYSC(syschat)->list_filter), "%s", parsed[1]);
	}
	SYSC(syschat)->listing = 1;
	SYSC(syschat)->listing_since = time(NULL);
	SYSC(syschat)->list_received = 0;
	if (mask[0])
		snprintf(message, BF_SIZE, "LIST %s\r\n", mask);
	else
		snprintf(message, BF_SIZE, "LIST\r\n");
	net_send(SYSC(syschat), message, strlen(message));
	bzero(line, sizeof(line));
	if (mask[0])
		snprintf(line, sizeof(line), "\e[0;37mListing %s ...\e[0m", mask);
	else
		snprintf(line, sizeof(line), "\e[0;37mListing channels ...\e[0m");
	buffers_append(syschat, index, line);
}

/*
** A help row is 78 columns at most: two cells of HELP_CELL, the first one
** padded, so nothing wraps on an 80-column terminal.
*/
#define HELP_CELL 40

/*
** /? -- one row per command when it fits, two per row otherwise. The list
** grows with every new command and this answer goes into the status ring,
** which the empty frame only shows the tail of: at one row per command the
** "Commands list" header had already scrolled out of a 24-line terminal. The
** columns keep every entry verbatim, they only share rows.
*/
void commands_handle_cmd_list(t_syschat *syschat)
{
	static const char *help[] = {
		"exit -> leave syschat",
		"connect <nick> <server> <host> [port] -> open another server",
		"disconnect [label] -> close a server",
		"server [label] -> list or show servers",
		"join #<channel> -> join a channel",
		"part [#<channel>] -> leave a channel",
		"close [#<channel>] -> same as part",
		"close <label>/<chan> -> another server",
		"close <label> -> close a connection",
		"list [motif] -> list server channels",
		"switch [#<chan>|<label>/#<chan>] -> show a buffer",
		"privmsg <nick> -> same as msg",
		"whois <nick> -> who is that user",
		"names [channel] -> list nicknames",
		"topic [#chan] [text] -> read or set a topic",
		"mode [target] [modes] -> show or set",
		"kick [#chan] <nick> -> kick a user",
		"nick <pseudo> -> change your nickname",
		"clear -> clear the current buffer",
		"msg <target> <text> -> send a message",
		"me <text> -> action in channel or query",
		"notice <nick> <text> -> NOTICE      quote <line> -> raw IRC line",
		"away [text] -> mark away/back       invite <nick> [#chan] -> invite",
		"ignore <nick|nick*> -> hide       ignore -<nick> -> show again",
		"lock <name> -> same as switch",
		"? -> this list",
		"Tab / Shift-Tab, next|prev [n] -> cycle buffers",
		"Alt-Tab / Alt-Shift-Tab -> cycle servers",
		NULL
	};
	char	line[BF_SIZE];
	size_t	left;

	commands_say(syschat, "Commands list");
	for (int i = 0; help[i]; i++)
	{
		left = strlen(help[i]);
		if (help[i + 1] && left < HELP_CELL && strlen(help[i + 1]) < HELP_CELL)
		{
			snprintf(line, sizeof(line), "%-*s%s", HELP_CELL - 1, help[i], help[i + 1]);
			commands_say(syschat, line);
			i++;
		}
		else
			commands_say(syschat, help[i]);
	}
}

void commands_handle_ctcp(t_syschat *syschat, char **parsed)
{
	char	message[BF_SIZE];

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "PRIVMSG %s :%c%s%c\r\n", parsed[1], 1, parsed[2], 1);
	net_send(SYSC(syschat), message, strlen(message));
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
	else if (strcmp(parsed[0], "whois") == 0)
		commands_handle_whois(syschat, parsed);
	else if (strcmp(parsed[0], "names") == 0)
		commands_handle_names(syschat, parsed);
	else if (strcmp(parsed[0], "topic") == 0)
		commands_handle_topic(syschat, command, parsed);
	else if (strcmp(parsed[0], "mode") == 0)
		commands_handle_mode(syschat, command, parsed);
	else if (strcmp(parsed[0], "kick") == 0)
		commands_handle_kick(syschat, command, parsed);
	else if (strcmp(parsed[0], "nick") == 0)
		commands_handle_nick(syschat, parsed);
	else if (strcmp(parsed[0], "clear") == 0)
		commands_handle_clear(syschat);
	else if (strcmp(parsed[0], "msg") == 0)
		commands_handle_msg(syschat, command);
	else if (strcmp(parsed[0], "me") == 0)
		commands_handle_me(syschat, command);
	else if (strcmp(parsed[0], "notice") == 0)
		commands_handle_notice(syschat, command);
	else if (strcmp(parsed[0], "quote") == 0 || strcmp(parsed[0], "raw") == 0)
		commands_handle_quote(syschat, command);
	else if (strcmp(parsed[0], "away") == 0 || strcmp(parsed[0], "back") == 0)
		commands_handle_away(syschat, command);
	else if (strcmp(parsed[0], "invite") == 0)
		commands_handle_invite(syschat, parsed);
	else if (strcmp(parsed[0], "ignore") == 0)
		commands_handle_ignore(syschat, parsed);
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
	else if (strcmp(parsed[0], "connect") == 0)
		commands_handle_connect(syschat, parsed);
	else if (strcmp(parsed[0], "disconnect") == 0)
		commands_handle_disconnect(syschat, parsed);
	else if (strcmp(parsed[0], "server") == 0 || strcmp(parsed[0], "servers") == 0)
		commands_handle_server(syschat, parsed);

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
