#include "syschat.h"
#include "utils/network.h"
#include "utils/error.h"
#include "utils/commands.h"
#include "utils/server.h"
#include <stdio.h>

static t_syschat syschat;
static struct termios oldt, newt;

// TODO: Find a way to cut the stdin input to simulate X axis scrolling (ioctl ?)
// TODO: Add CTCP stuff (just for fun)
// TODO: Use write instead of printf when it's possible


// TODO: Implement a char by char recv reading to go one line ("\r\n") at a time
//       - Needs some testing now !
//       - Might be slow
// TODO: Find a way to avoid "^?" to be shortly printed before printing the new buffer
//         - This only happens on alacritty for now. going for low priority on this one
// TODO: Add the possibility to manage multiple channel. for this i'll need to avoid using predefined channel name
//       - Needs some testing now !
// TODO: Add /PART command
//       - Needs some testing now !

void syschat_load_config(char **argv)
{
	syschat.hostname = strdup(argv[1]);
	syschat.nickname = strdup(argv[2]);
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
}

void syschat_handle_input(char *stdin_buffer, char *buffer)
{
	char message[BF_SIZE];
	char *mod_channel;

	bzero(message, BF_SIZE);
	if (buffer[0] == 127)
	{
		stdin_buffer[strlen(stdin_buffer) - 1] = '\0';
		write(1, "\033[2K\r", 5);
		if (syschat.channel)
			printf("%s > ", syschat.channel);
		else
			printf("> ");
		fflush(stdout);
		write(1, stdin_buffer, strlen(stdin_buffer));
		fflush(stdout);
		return ;
	}
	strcat(stdin_buffer, buffer);
	if (buffer[0] == '\n')
	{
		if (stdin_buffer[0] == '/')
			commands_execute(&syschat, stdin_buffer);
		else
		{
			if (!syschat.channel)
			{
				printf("\033M\r\033[2K\e[0;31mYou are not in a channel ! Use /join <channel>\e[0m\n");
				bzero(stdin_buffer, BF_SIZE);
				fflush(stdout);
				printf("> ");
				fflush(stdout);
				return ;
			}
			sprintf(message, "PRIVMSG %s :%s\r\n", syschat.channel, stdin_buffer);
			send(syschat.net_socket, message, strlen(message), MSG_WAITFORONE);
			if (syschat.channel[0] == '#')
				printf("\033M[\e[0;35m%s\e[0m] <\e[0;33m%s\e[0m>: \e[0;33m%s\e[0m", syschat.channel, syschat.nickname, stdin_buffer);
			else
				printf("\033M<\e[0;33m%s\e[0m> -> <\e[0;34m%s\e[0m>: \e[0;33m%s\e[0m", syschat.nickname, syschat.channel, stdin_buffer);
		}
		if (syschat.channel)
			printf("%s > ", syschat.channel);
		else
			printf("> ");
		bzero(stdin_buffer, BF_SIZE);
	}
	fflush(stdout);
}

void syschat_handle_message(char *stdin_buffer, char *buffer)
{
	server_handle_message(&syschat, buffer);
	printf("\r\033[2K%s\e[0m", buffer);
	if (syschat.channel)
		printf("%s > ", syschat.channel);
	else
		printf("> ");
	fflush(stdout);
	if (strlen(stdin_buffer) > 1)
	{
		printf("%s", stdin_buffer);
		stdin_buffer[strlen(stdin_buffer)] = '\0';
	}
	fflush(stdout);
}

void syschat_loop()
{
	char buffer[BF_SIZE];
	char tmp_buffer[2];
	char stdin_buffer[BF_SIZE];
	struct epoll_event evls[64];
	int did_say_hello = 0;

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
				syschat_handle_input(stdin_buffer, buffer);
			}
			else
			{
				if (!did_say_hello)
				{
					syschat_say_hello();
					did_say_hello = 1;
				}
				while (!strchr(buffer, '\n'))
				{
					bzero(tmp_buffer, 2);
					if (recv(syschat.net_socket, tmp_buffer, 1, 0) <= 0)
					{
						syschat.running = 0;
						break;
					}
					if (strlen(buffer) == 512)
					{
						buffer[509] = '\r';
						buffer[510] = '\n';
						buffer[511] = '\0';
						break;
					}
					strcat(buffer, tmp_buffer);
				}
				if (syschat.running)
					syschat_handle_message(stdin_buffer, buffer);
			}
		}
	}
}

int main(int argc, char **argv)
{
	if (argc < 2)
		error_exit(NULL, 1);

	syschat_load_config(argv);
	syschat_prepare_screen();

	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	fcntl(STDIN_FILENO, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

	printf("[%s]\n", SYSCHAT_QUIT);
	network_connect_server(&syschat);
	syschat_loop();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	error_cleaning(&syschat);
	return 0;
}
