#include "syschat.h"
#include "utils/network.h"
#include "utils/error.h"
#include "utils/commands.h"
#include "utils/server.h"

static t_syschat syschat;
static struct termios oldt, newt;

// TODO: Fix the return key printing "^?"

void syschat_load_config(char **argv)
{
	syschat.hostname = strdup(argv[1]);
	syschat.nickname = strdup(argv[2]);
	syschat.channel = strdup(argv[3]);
}

void syschat_prepare_screen()
{
	for (int i = 0; i != 1000; i++)
		if (write(1, "\n", 1) == -1)
			break ;
}

void syschat_say_hello()
{
	char message[BF_SIZE];

	send(syschat.net_socket, "PASS none\r\n", 11, MSG_DONTWAIT);

	bzero(message, BF_SIZE);
	sprintf(message, "NICK %s\r\n", syschat.nickname);
	send(syschat.net_socket, message, strlen(message), MSG_DONTWAIT);

	bzero(message, BF_SIZE);
	sprintf(message, "USER %s %s %s %s\r\n", syschat.nickname, syschat.nickname, syschat.nickname, syschat.nickname);
	send(syschat.net_socket, message, strlen(message), MSG_DONTWAIT);

	bzero(message, BF_SIZE);
	sprintf(message, "JOIN #%s\r\n", syschat.channel);
	send(syschat.net_socket, message, strlen(message), MSG_DONTWAIT);
}

void syschat_handle_input(char *stdin_buffer, char *buffer)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	strcat(stdin_buffer, buffer);
	if (buffer[0] == '\n')
	{
		if (stdin_buffer[0] == '/')
			commands_execute(&syschat, stdin_buffer);
		else
		{
			sprintf(message, "PRIVMSG #%s :%s\r\n", syschat.channel, stdin_buffer);
			send(syschat.net_socket, message, strlen(message), MSG_WAITFORONE);
		}
		switch (SYSCHAT_PRINT_MODE)
		{
			case 1:
				printf("\033M[%s@%s]$ %s", syschat.nickname, syschat.channel, stdin_buffer);
				break;
			case 2:
				printf("\033M[#%s] %s -> %s", syschat.channel, syschat.nickname, stdin_buffer);
				break;
			case 3:
				printf("\033M%s: %s> ", syschat.nickname, stdin_buffer);
				break;
			default:
				printf("\033M[#%s] <%s>: %s", syschat.channel, syschat.nickname, stdin_buffer);
				break;
		}
		bzero(stdin_buffer, BF_SIZE);
	}
}

void syschat_handle_message(char *stdin_buffer, char *buffer)
{
	server_handle_message(&syschat, buffer);
	printf("\r%s", buffer);
	fflush(stdout);
	if (strlen(stdin_buffer) > 1)
	{
		printf("> %s", stdin_buffer);
		stdin_buffer[strlen(stdin_buffer)] = '\0';
	}
	else
		printf("> ");
	fflush(stdout);
}

void syschat_loop()
{
	char buffer[BF_SIZE];
	char stdin_buffer[BF_SIZE];
	struct epoll_event evls[64];

	syschat.running = 1145;
	bzero(stdin_buffer, BF_SIZE);
	syschat_say_hello();
	printf("> ");
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
				syschat_handle_input(stdin_buffer, buffer);
			}
			else
			{
				if (recv(syschat.net_socket, buffer, BF_SIZE, 0) <= 0)
				{
					syschat.running = 0;
					break;
				}
				syschat_handle_message(stdin_buffer, buffer);
			}
		}
	}
}

int main(int argc, char **argv)
{
	if (argc != 4)
		error_exit(NULL, 1);

	syschat_load_config(argv);
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
