#include "error.h"
#include "buffers.h"

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
			fprintf(stderr, "you need to give <hostname> <nickname> arguments.");
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
		default:
			fprintf(stderr, "unknown error.");
	}
	fprintf(stderr, "\n");
}

void error_cleaning(t_syschat *syschat)
{
	if (!syschat)
		return;
	syschat->listing = 0;
	syschat->elist_mask = 0;

	if (syschat->net_socket)
		close(syschat->net_socket);
	if (syschat->hostname)
		free(syschat->hostname);
	if (syschat->nickname)
		free(syschat->nickname);
	buffers_free(syschat);
}
