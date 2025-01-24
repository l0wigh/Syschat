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
			printf("you need to give <hostname> <nickname> <channel>.");
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
		default:
			printf("unknown error.");
	}
	printf("\n");
}

void error_cleaning(t_syschat *syschat)
{
	if (!syschat)
		return;

	free(syschat->hostname);
	free(syschat->nickname);
	free(syschat->channel);
}
