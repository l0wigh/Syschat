#include "syschat.h"
#include "utils/network.h"
#include "utils/error.h"

static t_syschat syschat;
static struct termios oldt, newt;

void syschat_load_config(t_syschat *syschat, char **argv)
{
	syschat->hostname = strdup(argv[1]);
	syschat->nickname = strdup(argv[2]);
	syschat->channel = strdup(argv[3]);
}

void syschat_prepare_screen()
{
	for (int i = 0; i != 1000; i++)
		if (write(1, "\n", 1) == -1)
			break ;
}

int main(int argc, char **argv)
{
	if (argc != 4)
		error_exit(NULL, 1);

	syschat_load_config(&syschat, argv);
	syschat_prepare_screen();

	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	fcntl(STDIN_FILENO, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

	network_connect_server(&syschat);

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	error_cleaning(&syschat);
	return 0;
}
