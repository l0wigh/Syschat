#include "error.h"
#include "buffers.h"
#include "tls.h"

void error_exit(t_syschat *syschat, int err)
{
	error_print(err);
	error_cleaning(syschat);
	exit(err);
}

void error_print(int err)
{
	fprintf(stderr, "Error: ");
	switch (err)
	{
		case 1:
			fprintf(stderr, "you need to give <hostname> <nickname> arguments.\n"
				"usage: ./syschat <hostname> <nickname> [port] "
				"[--tls] [--no-tls] [--insecure] [--tls-required]");
			break;
		case 2:
			fprintf(stderr, "can't get the size of your terminal.");
			break;
		case 3:
			fprintf(stderr, "can't open /dev/tty.");
			break;
		case 4:
			fprintf(stderr, "can't find the ip for the hostname.");
			break;
		case 5:
			fprintf(stderr, "can't create network socket.");
			break;
		case 6:
			fprintf(stderr, "can't create server socket.");
			break;
		case 7:
			fprintf(stderr, "can't connect to server.");
			break;
		case 8:
			fprintf(stderr, "channel need to be prefixed with '#'.");
			break;
		case 9:
			fprintf(stderr, "port must be an integer between 1 and 65535.");
			break;
		case 10:
			fprintf(stderr, "TLS is required (--tls-required) but the handshake failed.");
			break;
		case 11:
			fprintf(stderr, "--no-tls and --tls-required contradict each other, pick one.");
			break;
		default:
			fprintf(stderr, "unknown error.");
	}
	fprintf(stderr, "\n");
}

void error_cleaning(t_syschat *syschat)
{
	t_conn	*conn;

	if (!syschat)
		return;
	syschat_terminal_restore();
	/*
	** Every connection is torn down, not just the first one: a multi-server
	** session would otherwise leave N-1 sockets and N-1 mbedTLS contexts
	** alive at exit. fd == -1 is the "no connection" sentinel (fd 0 is a
	** valid descriptor and used to be mistaken for "nothing to close").
	*/
	for (int i = 0; i != syschat->conn_count; i++)
	{
		conn = &syschat->conns[i];
		conn->listing = 0;
		conn->elist_mask = 0;
		if (conn->fd >= 0)
		{
			tls_close(conn);
			close(conn->fd);
			conn->fd = -1;
		}
		conn->alive = 0;
		if (conn->label)
			free(conn->label);
		if (conn->hostname)
			free(conn->hostname);
		if (conn->nickname)
			free(conn->nickname);
		conn->label = NULL;
		conn->hostname = NULL;
		conn->nickname = NULL;
	}
	for (int i = 0; i != SYSCHAT_STATUS_LINES; i++)
	{
		if (syschat->status[i])
			free(syschat->status[i]);
		syschat->status[i] = NULL;
	}
	buffers_free(syschat);
}

void error_tls_exit(t_syschat *syschat, const char *reason)
{
	/*
	** error_print() only knows fixed messages; the handshake reason is
	** runtime text, and it is the only thing that tells the user whether to
	** fix their clock, their CA bundle or their port.
	*/
	fprintf(stderr, "Error: TLS is required but the handshake failed: %s\n", reason);
	error_cleaning(syschat);
	exit(10);
}
