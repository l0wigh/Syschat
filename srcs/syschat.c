#include "syschat.h"
#include "utils/network.h"
#include "utils/error.h"

static t_syschat syschat;
static struct termios oldt, newt;
static int BF_SIZE = 512;

// TODO: Clean the looping function
// TODO: Fix the return key printing "^?"
// TODO: Add Hello before looping !

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

void syschat_loop()
{
	char buffer[BF_SIZE];
	char stdin_buffer[BF_SIZE];
	struct epoll_event evls[64];

	syschat.running = 1145;
	bzero(stdin_buffer, BF_SIZE);
	while (syschat.running)
	{
		int num_evls = epoll_wait(syschat.epoll_fd, (struct epoll_event *)&evls, 64, 500);
		for (int i = 0; i < num_evls; i++)
		{
			bzero(buffer, BF_SIZE);
			if (evls[i].data.fd == STDIN_FILENO)
			{
				if (read(STDIN_FILENO, buffer, BF_SIZE) <= 0)
					continue;
				strcat(stdin_buffer, buffer);
				if (buffer[0] == '\n')
				{
					printf("\033M%s: %s", syschat.nickname, stdin_buffer);
					strcat(stdin_buffer, "\r\n");
					send(syschat.net_socket, stdin_buffer, BF_SIZE, MSG_WAITFORONE);
					bzero(stdin_buffer, BF_SIZE);
				}
			}
			else
			{
				if (recv(syschat.net_socket, buffer, BF_SIZE, 0) <= 0)
				{
					syschat.running = 0;
					break;
				}
				printf("\r%s", buffer);
				fflush(stdout);
				if (strlen(stdin_buffer) > 1)
				{
					/* stdin_buffer[strlen(stdin_buffer)] = '\n'; */
					printf("%s", stdin_buffer);
					/* fflush(stdout); */
					/* printf("\033[%luC", strlen(stdin_buffer) - 1); */
					fflush(stdout);
					stdin_buffer[strlen(stdin_buffer)] = '\0';
				}
			}
		}
	}
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
	syschat_loop();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	error_cleaning(&syschat);
	return 0;
}
