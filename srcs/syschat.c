#include "syschat.h"
#include "utils/network.h"
#include "utils/tls.h"
#include "utils/error.h"
#include "utils/commands.h"
#include "utils/server.h"
#include "utils/buffers.h"
#include "utils/screen.h"
#include "segfault_catcher/segfault_catcher.h"

#include <errno.h>
#include <signal.h>

static t_syschat syschat;
static struct termios oldt, newt;
static int tty_raw;

void syschat_terminal_raw(void)
{
	if (tty_raw || tcgetattr(STDIN_FILENO, &oldt) != 0)
		return ;
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	newt.c_oflag &= ~OPOST;
	newt.c_cc[VMIN] = 1;
	newt.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	tty_raw = 1;
}

/* Called from error_cleaning() too: a fatal error after raw mode must not
** leave the shell without echo. */
void syschat_terminal_restore(void)
{
	if (!tty_raw)
		return ;
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	tty_raw = 0;
}

/*
** Password prompt, before raw mode: the line discipline stays canonical (the
** user keeps backspace and Ctrl-C) and only ECHO is turned off. The saved
** termios is restored on every exit path -- normal end, EOF, and a fatal
** signal -- because Ctrl-C here would otherwise leave the user's shell typed
** blind.
*/
static struct termios	pass_saved;
static volatile sig_atomic_t	pass_echo_off;

static void	pass_restore_echo(void)
{
	if (pass_echo_off)
	{
		tcsetattr(STDIN_FILENO, TCSANOW, &pass_saved);
		pass_echo_off = 0;
	}
}

static void	pass_signal(int sig)
{
	/* tcsetattr() and write() are async-signal-safe: the terminal comes back
	** even when the signal lands in the middle of the read. */
	pass_restore_echo();
	(void) write(STDOUT_FILENO, "\n", 1);
	_exit(128 + sig);
}

/*
** Returns 0 on success, 1 when nothing was typed (empty line or end of
** input) and -1 when the value does not fit. A truncated password would fail
** to authenticate with a message that blames the server, so an oversized one
** is refused instead of being silently shortened.
*/
#define PASS_READ_OK 0
#define PASS_READ_EMPTY 1
#define PASS_READ_TOO_LONG -1

static int	syschat_ask_password(char *dst, size_t size, const char *prompt)
{
	static const int	sigs[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT };
	struct sigaction	sa;
	struct sigaction	old[4];
	struct termios		noecho;
	int					tty;
	int					too_long;
	size_t				len;
	ssize_t				read_len;
	size_t				i;
	char				c;

	dst[0] = '\0';
	len = 0;
	too_long = 0;
	tty = isatty(STDIN_FILENO);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = pass_signal;
	sigemptyset(&sa.sa_mask);
	/* No SA_RESTART: the handler exits, nothing is resumed anyway. */
	sa.sa_flags = 0;
	for (i = 0; i != sizeof(sigs) / sizeof(sigs[0]); i++)
		sigaction(sigs[i], &sa, &old[i]);
	/* A pipe or a file is not a terminal: read it as is, change nothing. */
	if (tty && tcgetattr(STDIN_FILENO, &pass_saved) == 0)
	{
		noecho = pass_saved;
		noecho.c_lflag &= ~ECHO;
		if (tcsetattr(STDIN_FILENO, TCSANOW, &noecho) == 0)
			pass_echo_off = 1;
	}
	(void) write(STDOUT_FILENO, prompt, strlen(prompt));
	while ((read_len = read(STDIN_FILENO, &c, 1)) != 0)
	{
		if (read_len < 0)
		{
			if (errno == EINTR)
				continue ;
			break ;
		}
		if (c == '\n')
			break ;
		/* CR can only come from a pasted CRLF: dropping it keeps a stray
		** CR out of the IRC line (a CR there would end the command and let
		** the rest of the password run as a separate one). */
		if (c == '\r')
			continue ;
		if (len + 1 >= size)
		{
			too_long = 1;
			continue ;
		}
		dst[len++] = c;
	}
	dst[len] = '\0';
	pass_restore_echo();
	for (i = 0; i != sizeof(sigs) / sizeof(sigs[0]); i++)
		sigaction(sigs[i], &old[i], NULL);
	(void) write(STDOUT_FILENO, "\n", 1);
	if (too_long)
		return (PASS_READ_TOO_LONG);
	if (dst[0] == '\0')
		return (PASS_READ_EMPTY);
	return (PASS_READ_OK);
}
static char input[SYSCHAT_LINE_LEN];
static int input_len = 0;

#define NET_BUFFER_SIZE 16384

// TODO: Fix stack smashing when pasting too much characters
// TODO: Use write instead of printf when it's possible

/*
** Process wide TLS policy, decided from the command line and the
** environment. It is applied to the connection given on the command line
** AND to every server added later with /connect: the rule must not change
** depending on when the connection was opened.
*/
static int	policy_tls;
static int	policy_no_tls;
static int	policy_strict;
static int	policy_insecure;
/*
** The password is asked ONCE, before raw mode and before any connection:
** it is process wide state, like the TLS policy, so a server added later
** with /connect gets it without prompting again. Empty means "PASS none".
*/
static int	policy_pass;
static char	policy_password[SYSCHAT_PASS_LEN];

/* Zero a password before handing its memory back, so it does not linger in
** freed heap blocks. */
static void	pass_wipe(char *password)
{
	if (password)
		memset(password, 0, strlen(password));
}

void	syschat_apply_pass_policy(t_conn *conn)
{
	if (!conn || policy_password[0] == '\0')
		return ;
	pass_wipe(conn->password);
	free(conn->password);
	conn->password = strdup(policy_password);
}

void	syschat_apply_tls_policy(t_conn *conn)
{
	char	*env;

	conn->tls = (conn->port == 6697);
	if ((env = getenv("SYSCHAT_TLS")) != NULL && env[0] != '\0')
		conn->tls = !strcmp(env, "1") || !strcmp(env, "true") || !strcmp(env, "yes");
	conn->tls_insecure = policy_insecure;
	if ((env = getenv("SYSCHAT_TLS_NO_VERIFY")) != NULL
		&& (!strcmp(env, "1") || !strcmp(env, "true") || !strcmp(env, "yes")))
		conn->tls_insecure = 1;
	if (policy_tls)
		conn->tls = 1;
	if (policy_no_tls)
		conn->tls = 0;
	/*
	** Strict = "never plaintext". It overrides --no-tls (refused at parse
	** time) and turns a failed handshake into a hard error instead of a
	** silent fallback, for this connection like for the first one.
	*/
	if (policy_strict)
	{
		conn->tls_required = 1;
		conn->tls = 1;
	}
	if (conn->tls_insecure && !policy_no_tls)
		conn->tls = 1;
}

/*
** Claims a connection slot. Slots are never moved: a client-side id is the
** index in syschat->conns[] and buffers store it forever.
*/
static int	syschat_conn_slot(t_syschat *syschat)
{
	int	i;

	for (i = 0; i != syschat->conn_count; i++)
		if (syschat->conns[i].fd < 0 && !syschat->conns[i].alive
			&& !syschat->conns[i].hostname)
			return (i);
	if (syschat->conn_count >= SYSCHAT_MAX_SERVERS)
		return (-1);
	return (syschat->conn_count++);
}

int	syschat_conn_add(t_syschat *syschat, const char *label, const char *host,
	const char *nickname, int port)
{
	t_conn	*conn;
	int		id;

	id = syschat_conn_slot(syschat);
	if (id < 0)
		return (-1);
	conn = &syschat->conns[id];
	bzero(conn, sizeof(t_conn));
	conn->fd = -1;
	conn->alive = 1;
	conn->owner = syschat;
	conn->label = strdup(label);
	conn->hostname = strdup(host);
	conn->nickname = strdup(nickname);
	conn->port = port;
	if (!conn->label || !conn->hostname || !conn->nickname)
	{
		free(conn->label);
		free(conn->hostname);
		free(conn->nickname);
		conn->label = conn->hostname = conn->nickname = NULL;
		conn->alive = 0;
		return (-1);
	}
	syschat_apply_tls_policy(conn);
	syschat_apply_pass_policy(conn);
	return (id);
}

/*
** Name of a connection's console buffer: the label, never the host name.
** With a label different from the host, routing server-level lines to
** conn->hostname opened a SECOND console tab for the same server, and the
** name-based kind inference classified it as a private message.
*/
const char	*syschat_conn_name(const t_conn *conn)
{
	if (!conn)
		return ("?");
	if (conn->label && conn->label[0])
		return (conn->label);
	return (conn->hostname ? conn->hostname : "?");
}

/*
** /ignore is a display filter, not a server-side behaviour: the message
** arrives and is dropped before anything is formatted, so an ignored nick
** cannot open a buffer or trigger an automatic answer either.
** A mask is either an exact nick (case-insensitive) or a prefix ("foo*").
*/
int	syschat_is_ignored(t_syschat *syschat, const char *nick)
{
	size_t	len;

	if (!syschat || !nick || !nick[0])
		return (0);
	for (int i = 0; i != syschat->ignore_count; i++)
	{
		len = strlen(syschat->ignore[i]);
		if (len && syschat->ignore[i][len - 1] == '*')
		{
			if (strncasecmp(syschat->ignore[i], nick, len - 1) == 0)
				return (1);
		}
		else if (strcasecmp(syschat->ignore[i], nick) == 0)
			return (1);
	}
	return (0);
}

void	syschat_conn_close(t_syschat *syschat, int id)
{
	t_conn	*conn;

	if (!syschat || id < 0 || id >= syschat->conn_count)
		return ;
	conn = &syschat->conns[id];
	if (!conn->alive)
		return ;
	conn->alive = 0;
	if (conn->fd >= 0)
	{
		tls_close(conn);
		close(conn->fd);
		conn->fd = -1;
	}
	conn->net_len = 0;
	conn->listing = 0;
	conn->elist_mask = 0;
	conn->whois_nick[0] = '\0';
	/*
	** Relabel first, free after: buffers_relabel() reads conns[id].label
	** to prefix the surviving tabs, so freeing it earlier would hand it a
	** dangling pointer as soon as a second server keeps a homonymous tab.
	*/
	buffers_leave_server(syschat, (unsigned) id);
	free(conn->label);
	free(conn->hostname);
	free(conn->nickname);
	pass_wipe(conn->password);
	free(conn->password);
	conn->label = NULL;
	conn->hostname = NULL;
	conn->nickname = NULL;
	conn->password = NULL;
	/*
	** Pure cleanup: closing a slot says nothing about whether the session
	** is over. Ending the loop is decided by the callers that actually lost
	** a live server -- a /connect that never worked must not close the app.
	*/
}

int	syschat_any_alive(t_syschat *syschat)
{
	if (!syschat)
		return (0);
	for (int i = 0; i != syschat->conn_count; i++)
		if (syschat->conns[i].alive)
			return (1);
	return (0);
}

/*
** Flags are recognised anywhere on the command line, positionals keep their
** historic order (<hostname> <nickname> [port]) so every documented command
** still works unchanged. TLS is never on by default on 6667: it is enabled
** by --tls, or implicitly when the port is 6697.
*/
void syschat_load_config(char **argv, int argc)
{
	char	*positional[3];
	int		i;
	int		pos;
	int		port;
	t_conn	*conn;

	bzero(&syschat, sizeof(t_syschat));
	for (i = 0; i != SYSCHAT_MAX_SERVERS; i++)
		syschat.conns[i].fd = -1;
	bzero(positional, sizeof(positional));
	pos = 0;
	policy_tls = 0;
	policy_no_tls = 0;
	policy_strict = 0;
	policy_insecure = 0;
	/* --pass takes no argument: the value is typed at the prompt, never on
	** the command line where `ps` would show it. */
	policy_pass = 0;
	memset(policy_password, 0, sizeof(policy_password));
	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--tls") || !strcmp(argv[i], "-tls"))
			policy_tls = 1;
		else if (!strcmp(argv[i], "--no-tls") || !strcmp(argv[i], "-no-tls"))
			policy_no_tls = 1;
		else if (!strcmp(argv[i], "--insecure") || !strcmp(argv[i], "-insecure"))
			policy_insecure = 1;
		else if (!strcmp(argv[i], "--tls-required") || !strcmp(argv[i], "-tls-required"))
			policy_strict = 1;
		else if (!strcmp(argv[i], "--pass") || !strcmp(argv[i], "-pass"))
			policy_pass = 1;
		else if (pos < 3)
			positional[pos++] = argv[i];
	}
	/*
	** No argument at all: start disconnected. /connect exists now, so the
	** client no longer has to die on the command that the usage string
	** used to demand. "host without nickname" stays an error.
	*/
	if (pos == 0)
	{
		pos = 0;
	}
	else if (pos < 2)
		error_exit(NULL, 1);
	/* No port means the TLS port, exactly like /connect: one default for
	** every way in, so "syschat host nick" and "/connect n s host" agree. */
	port = 6697;
	if (pos > 2)
	{
		char	*end;
		long	parsed;

		end = NULL;
		parsed = strtol(positional[2], &end, 10);
		if (end == positional[2] || *end != '\0' || parsed < 1 || parsed > 65535)
			error_exit(NULL, 9);
		port = (int) parsed;
	}
	if ((getenv("SYSCHAT_TLS_STRICT") != NULL)
		&& (!strcmp(getenv("SYSCHAT_TLS_STRICT"), "1")
		|| !strcmp(getenv("SYSCHAT_TLS_STRICT"), "true")
		|| !strcmp(getenv("SYSCHAT_TLS_STRICT"), "yes")))
		policy_strict = 1;
	if (policy_strict && policy_no_tls)
		error_exit(NULL, 11);
	if (pos == 0)
	{
		/* No server and no nickname: /connect asks for both. */
		syschat.conn_count = 0;
		syschat.current = 0;
		syschat.active = -1;
		return ;
	}
	syschat.conn_count = 1;
	conn = &syschat.conns[0];
	conn->fd = -1;
	conn->alive = 1;
	conn->owner = &syschat;
	conn->label = strdup(positional[0]);
	conn->hostname = strdup(positional[0]);
	conn->nickname = strdup(positional[1]);
	conn->port = port;
	syschat_apply_tls_policy(conn);
	syschat.current = 0;
	syschat.active = -1;
	buffers_new_on(&syschat, 0, conn->label);
	buffers_switch(&syschat, 0);
}

void syschat_say_hello(t_conn *conn)
{
	char message[BF_SIZE];
	const char *password;

	/*
	** The password replaces the historic "PASS none". IRC has no escaping
	** inside a parameter, so a value holding a space (a ZNC
	** "user/network:password" does not, but a passphrase can) or starting
	** with ':' must use the trailing form, the only one that can carry it.
	*/
	password = (conn && conn->password && conn->password[0]) ? conn->password : NULL;
	if (password && (password[0] == ':' || strchr(password, ' ') != NULL))
		snprintf(message, BF_SIZE, "PASS :%s\r\n", password);
	else if (password)
		snprintf(message, BF_SIZE, "PASS %s\r\n", password);
	else
		snprintf(message, BF_SIZE, "PASS none\r\n");
	net_send(conn, message, strlen(message));
	/* The line above is stamped with the password: do not let it sit in the
	** stack frame once it has been handed to the socket. */
	pass_wipe(message);

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "NICK %s\r\n", conn->nickname);
	net_send(conn, message, strlen(message));

	bzero(message, BF_SIZE);
	snprintf(message, BF_SIZE, "USER %s %s %s %s\r\n", conn->nickname, conn->nickname,
		conn->nickname, conn->nickname);
	net_send(conn, message, strlen(message));
}

void syschat_status(t_syschat *syschat, const char *text)
{
	int	i;

	if (!syschat || !text)
		return ;
	if (syschat->status[0])
		free(syschat->status[0]);
	for (i = 0; i + 1 < SYSCHAT_STATUS_LINES; i++)
		syschat->status[i] = syschat->status[i + 1];
	syschat->status[SYSCHAT_STATUS_LINES - 1] = strdup(text);
	if (!syschat->status[SYSCHAT_STATUS_LINES - 1])
		return ;
	if (syschat->status_count < SYSCHAT_STATUS_LINES)
		syschat->status_count++;
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
	char		*name;
	char		line[BF_SIZE * 2];
	const char	*sent;
	int			truncated;

	name = buffers_active_name(&syschat);
	/*
	** The old guard was `active <= 0`, i.e. "index 0 is the server window".
	** That invariant dies with a second server: its console window sits at
	** an index > 0 and a PRIVMSG to irc.example.org would be sent. The kind
	** of the buffer is what decides, not its position.
	*/
	if (!name || buffers_active_kind(&syschat) == BUF_SERVER
		|| buffers_active_kind(&syschat) == BUF_LIST)
	{
		syschat_echo("\e[0;31mNot in a channel or a private message. Use /join <channel>\e[0m");
		return ;
	}
	sent = send_privmsg(&syschat, name, input, &truncated);
	if (!sent)
		return ;
	bzero(line, sizeof(line));
	/* The tab title already names the channel or the correspondent: a private
	** message is echoed exactly like a channel one. */
	snprintf(line, sizeof(line), "<%s%s\e[0m> %s",
		nick_color(SYSC(&syschat)->nickname), SYSC(&syschat)->nickname, sent);
	buffers_append(&syschat, syschat.active, line);
	if (truncated)
	{
		bzero(line, sizeof(line));
		snprintf(line, sizeof(line), "\e[0;31mmessage tronqué à %d/%d caractères (limite serveur 512 octets)\e[0m",
			screen_text_width(sent), screen_text_width(input));
		buffers_append(&syschat, syschat.active, line);
	}
}

/*
** Modifier bits as xterm "modifyOtherKeys" and the kitty keyboard protocol
** encode them in a key report (the reported number is 1 + the bitmask).
*/
#define KEY_MOD_ALT  2
#define KEY_MOD_CTRL 4

/*
** A Tab report with modifiers. Shift alone is the classic Shift-Tab (go back
** one buffer). Any Alt or Ctrl report changes server: Ctrl-Tab and
** Ctrl-Shift-Tab are what the user asked for, Alt-Tab / Alt-Shift-Tab are the
** portable form because most terminals never emit a distinct Ctrl-Tab.
*/
static void	tab_key(int mods)
{
	if (mods & (KEY_MOD_ALT | KEY_MOD_CTRL))
		buffers_cycle_server(&syschat, (mods & 1) ? -1 : 1);
	else
		buffers_cycle(&syschat, (mods & 1) ? -1 : 1);
}

/*
** Byte oriented input handling: keeps reading possible even when a whole
** escape sequence (Shift-Tab = ESC [ Z) is split across two reads.
**
** Key map:
**   Tab                     -> next buffer   (current server only)
**   ESC [ Z                 -> previous buffer
**   ESC Tab                 -> next server   (Alt-Tab, X10 encoding)
**   ESC ESC [ Z             -> previous server (Alt-Shift-Tab)
**   ESC [ 9 ; <mod> u       -> kitty keyboard protocol Tab report
**   ESC [ 27 ; <mod> ; 9 ~  -> xterm modifyOtherKeys Tab report
**
** There is no escape sequence for Ctrl-Tab in a plain xterm: the terminal
** sends the same 0x09 as Tab, so the two are indistinguishable there. Only
** terminals speaking the kitty protocol (or modifyOtherKeys) report Ctrl-Tab
** on its own; the Alt form is what works everywhere.
*/
void syschat_handle_input(char *buffer, int size)
{
	static int	escape = 0;
	static char	seq[16];
	static int	seq_len = 0;
	static int	input_full = 0;
	int			index;
	int			key;
	int			mods;

	for (index = 0; index != size; index++)
	{
		char c = buffer[index];

		if (escape == 1)
		{
			if (c == '[' || c == 'O')
			{
				escape = 2;
				seq_len = 0;
			}
			else if (c == '	')
			{
				escape = 0;
				buffers_cycle_server(&syschat, 1);
			}
			else if (c == '\033')
				escape = 3;
			else
				escape = 0;
			continue ;
		}
		if (escape == 3)
		{
			/* Second ESC: this is Alt-Shift-Tab (ESC ESC [ Z). */
			if (c == '[' || c == 'O')
			{
				escape = 4;
				seq_len = 0;
			}
			else
				escape = 0;
			continue ;
		}
		if (escape == 2 || escape == 4)
		{
			if (c >= 0x20 && c <= 0x3f)
			{
				if (seq_len < (int) sizeof(seq) - 1)
					seq[seq_len++] = c;
				continue ;
			}
			seq[seq_len] = '\0';
			if (c == 'Z')
			{
				if (escape == 4)
					buffers_cycle_server(&syschat, -1);
				else
					buffers_cycle(&syschat, -1);
			}
			else if (c == 'u')
			{
				if (sscanf(seq, "%d;%d", &key, &mods) == 2 && key == 9)
					tab_key(mods - 1);
			}
			else if (c == '~')
			{
				if (sscanf(seq, "27;%d;%d", &mods, &key) == 2 && key == 9)
					tab_key(mods - 1);
			}
			escape = 0;
			continue ;
		}
		if (c == '\033')
			escape = 1;
		else if (c == '	')
			buffers_cycle(&syschat, 1);
		else if (c == 127 || c == 8)
		{
			/* Step back over the whole UTF-8 character, not one byte. */
			if (input_len > 0)
				input_len--;
			while (input_len > 0 && ((unsigned char) input[input_len] & 0xC0) == 0x80)
				input_len--;
			input[input_len] = '\0';
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
			input_full = 0;
		}
		else if ((unsigned char) c >= 32)
		{
			if (input_len + 1 >= SYSCHAT_LINE_LEN)
			{
				/* A paste longer than the line buffer used to vanish here,
				** silently, before the send path could warn about it. */
				if (!input_full)
				{
					input_full = 1;
					char	warn[128];

					snprintf(warn, sizeof(warn),
						"\e[0;31minput full: %d characters max, the rest is ignored\e[0m",
						SYSCHAT_LINE_LEN - 1);
					buffers_append(&syschat, syschat.active, warn);
				}
			}
			else
			{
				input[input_len++] = c;
				input[input_len] = '\0';
			}
		}
	}
	screen_draw(&syschat, input);
}

/*
** The socket is drained into a per-connection accumulator: a chunk can
** hold several IRC lines, or half of one. Sharing this buffer between
** sockets would glue half a line from A to a line from B -- plausible text,
** wrong line, and no error anywhere.
*/
void syschat_read_network(t_conn *conn)
{
	t_syschat	*syschat;
	char		srv[BF_SIZE];
	char		target[SYSCHAT_NAME_LEN];
	char		*end;
	char		chunk[2048];
	char		label[SYSCHAT_NAME_LEN];
	unsigned	sid;
	int			received;
	int			line_len;
	int			index;

	if (!conn || !conn->alive || conn->fd < 0)
		return ;
	syschat = conn->owner;
	sid = (unsigned) (conn - syschat->conns);
	bzero(label, sizeof(label));
	snprintf(label, sizeof(label), "%s", conn->label ? conn->label : "server");
	bzero(chunk, sizeof(chunk));
	received = net_recv(conn, chunk, sizeof(chunk) - 1);
	if (received == -1)
		return ;
	if (received == 0 || received == -2)
	{
		char	line[BF_SIZE];

		/*
		** One server lost: its buffers go, the others keep running. The
		** notice is written AFTER the close, so buffers_notice() picks a
		** window that still exists -- writing it before would hand the
		** text to the very tab that is about to be freed.
		*/
		syschat_conn_close(syschat, (int) sid);
		if (!syschat_any_alive(syschat))
			syschat->running = 0;
		snprintf(line, sizeof(line), "\e[0;31m-- %s disconnected\e[0m", label);
		buffers_notice(syschat, sid, line);
		screen_draw(syschat, input);
		return ;
	}
	if (received > SYSCHAT_NET_BUFFER - 1 - conn->net_len)
		conn->net_len = 0;
	memcpy(conn->net + conn->net_len, chunk, received);
	conn->net_len += received;
	conn->net[conn->net_len] = '\0';
	while ((end = strchr(conn->net, '\n')) != NULL)
	{
		line_len = end - conn->net;
		while (line_len > 0 && (conn->net[line_len - 1] == '\r' || conn->net[line_len - 1] == '\0'))
			line_len--;
		if (line_len > 0)
		{
			bzero(srv, sizeof(srv));
			snprintf(srv, sizeof(srv), "%.*s", line_len, conn->net);
			bzero(target, SYSCHAT_NAME_LEN);
			if (server_handle_message(syschat, conn, srv, target))
			{
				if (target[0] == '\0')
					snprintf(target, SYSCHAT_NAME_LEN, "%s", syschat_conn_name(conn));
				index = buffers_route_on(syschat, sid, target);
				if (index < 0)
				{
					/* 16 buffers are shared by every server: do not
					** swallow the line, say that the table is full. */
					buffers_notice(syschat, sid,
						"\e[0;31m-- no free buffer, line dropped --\e[0m");
				}
				else
					buffers_append(syschat, index, srv);
			}
		}
		memmove(conn->net, end + 1, conn->net_len - (int) (end + 1 - conn->net) + 1);
		conn->net_len -= (int) (end + 1 - conn->net);
	}
	screen_draw(syschat, input);
}

void syschat_loop(void)
{
	char			buffer[512];
	struct pollfd	*fds;
	int				*map;
	int				ret;
	int				n;
	int				pending;
	int				i;

	fds = calloc(SYSCHAT_MAX_SERVERS + 1, sizeof(struct pollfd));
	map = calloc(SYSCHAT_MAX_SERVERS + 1, sizeof(int));
	if (!fds || !map)
		return ;
	syschat.running = 1;
	while (syschat.running)
	{
		n = 1;
		fds[0].fd = STDIN_FILENO;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		pending = 0;
		for (i = 0; i != syschat.conn_count; i++)
		{
			if (!syschat.conns[i].alive || syschat.conns[i].fd < 0)
				continue ;
			/* With TLS a record can hold more bytes than one chunk, so
			** any socket may keep decrypted data while staying silent. */
			if (net_pending(&syschat.conns[i]))
				pending = 1;
			fds[n].fd = syschat.conns[i].fd;
			fds[n].events = POLLIN;
			fds[n].revents = 0;
			map[n] = i;
			n++;
		}
		ret = poll(fds, n, pending ? 0 : 500);
		if (ret < 0)
			continue ;
		if (fds[0].revents & POLLIN)
		{
			ret = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
			if (ret > 0)
				syschat_handle_input(buffer, ret);
		}
		for (i = 1; i < n; i++)
			if (fds[i].revents & (POLLIN | POLLHUP | POLLERR))
				syschat_read_network(&syschat.conns[map[i]]);
		for (i = 0; i != syschat.conn_count; i++)
		{
			t_conn	*conn = &syschat.conns[i];

			if (!conn->alive || conn->fd < 0)
				continue ;
			if (net_pending(conn))
				syschat_read_network(conn);
			if (conn->listing && time(NULL) - conn->listing_since >= 5)
			{
				int index = buffers_find_on(&syschat, (unsigned) i, SYSCHAT_LIST_BUFFER);

				conn->listing = 0;
				if (index != -1)
					buffers_append(&syschat, index,
						"\e[0;31m-- listing stopped after 5s, server sent nothing else\e[0m");
				screen_draw(&syschat, input);
			}
		}
	}
	free(fds);
	free(map);
}

int main(int argc, char **argv)
{
	segfault_catcher_set(0);

	/* syschat_load_config() decides: no argument starts disconnected,
	** "host without nickname" is still a usage error. */
	syschat_load_config(argv, argc);
	screen_setup(&syschat);

	/*
	** Asked here on purpose: after the flags are known, before raw mode (the
	** line discipline stays canonical and only ECHO is dropped), and before
	** the connection so syschat_say_hello() already has it. Asking once
	** makes it a process wide policy: /connect reuses it, never re-prompts.
	*/
	if (policy_pass)
	{
		int		status;
		int		i;

		status = syschat_ask_password(policy_password, sizeof(policy_password),
				"Password: ");
		/* A shortened password fails to authenticate and blames the
		** server: refuse at startup instead, with a message of our own. */
		if (status == PASS_READ_TOO_LONG)
			error_exit(NULL, 12);
		/* --pass without a password is a configuration mistake: say it
		** loudly, then keep the historic "PASS none" behaviour. */
		if (status == PASS_READ_EMPTY)
			fprintf(stderr, "syschat: warning: --pass given but no password was read "
				"(empty line or end of input); sending PASS none\n");
		for (i = 0; i != syschat.conn_count; i++)
			if (syschat.conns[i].alive)
				syschat_apply_pass_policy(&syschat.conns[i]);
	}

	syschat_terminal_raw();

	if (syschat.conn_count > 0)
	{
		char	banner[256];
		char	info[160];

		network_connect_server(&syschat, &syschat.conns[0], 1);
		if (syschat.conns[0].tls && !syschat.conns[0].tls_active)
		{
			char	warn[BF_SIZE];

			snprintf(warn, sizeof(warn),
				"\e[0;31mTLS: handshake failed (%s) -- falling back to plaintext, "
				"this session is NOT encrypted\e[0m",
				syschat.conns[0].tls_reason[0] ? syschat.conns[0].tls_reason : "no answer");
			buffers_append(&syschat, 0, warn);
		}
		syschat_say_hello(&syschat.conns[0]);
		tls_summary(&syschat.conns[0], info, sizeof(info));
		snprintf(banner, sizeof(banner),
			"Connected (%s). /join #channel to start, Tab / Shift-Tab to switch, /? for help.",
			info);
		syschat_echo(banner);
	}
	screen_draw(&syschat, input);
	syschat_loop();

	/*
	** The loop also ends when the last server hangs up: nothing is left to
	** draw into, so the explanation goes to stdout, after the event loop
	** and before the terminal is handed back.
	*/
	{
		int	alive;

		alive = 0;
		for (int i = 0; i != syschat.conn_count; i++)
			if (syschat.conns[i].alive)
				alive++;
		if (!alive)
			screen_message("\r\n-- no server left, exiting\r\n");
	}
	syschat_terminal_restore();
	error_cleaning(&syschat);
	return (0);
}
