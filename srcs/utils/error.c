#include "error.h"

void error_exit(t_syschat *syschat, int err)
{
	error_print(err);
	error_cleaning(syschat);
	exit(err);
}

void error_print(int err)
{
	printf("Error: ");
	switch (err)
	{
		case 1:
			printf("you need to give <hostname> <nickname> <channel (optionnal)>.");
			break;
		case 2:
			printf("can't get the size of your terminal.");
			break;
		case 3:
			printf("can't open /dev/tty.");
			break;
		case 4:
			printf("can't find the ip for the hostname.");
			break;
		case 5:
			printf("can't create network socket.");
			break;
		case 6:
			printf("can't create server socket.");
			break;
		case 7:
			printf("can't connect to server.");
			break;
		case 8:
			printf("channel need to be prefixed with '#'.");
			break;
		default:
			printf("unknown error.");
	}
	printf("\n");
}

void error_cleaning(t_syschat *syschat)
{
	if (!syschat)
		return;

	if (syschat->net_socket)
		close(syschat->net_socket);
	if (syschat->hostname)
		free(syschat->hostname);
	if (syschat->nickname)
		free(syschat->nickname);
	if (syschat->channel)
		free(syschat->channel);
}
