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
				"[--tls] [--no-tls] [--insecure]");
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
	if (!syschat)
		return;
	syschat_terminal_restore();
	syschat->listing = 0;
	syschat->elist_mask = 0;

	if (syschat->net_socket)
	{
		tls_close(syschat);
		close(syschat->net_socket);
	}
	if (syschat->hostname)
		free(syschat->hostname);
	if (syschat->nickname)
		free(syschat->nickname);
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
