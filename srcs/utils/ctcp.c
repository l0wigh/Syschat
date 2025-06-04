#include "ctcp.h"

char *ctcp_get_distro()
{
	char line[256];
	static char value[256];

	FILE *file = fopen("/etc/lsb-release", "r");

	if (!file)
		return (char *)os_ver;

	fgets(line, sizeof(line), file);
	for (int i = 12; line[i] != '\"'; i++)
	{
		if (i - 11 > 250)
			break ;
		value[i - 12] = line[i];
	}
	fclose(file);

	return value;
}

void ctcp_handle_request(t_syschat *syschat, char **parsed)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	if (parsed[3][1] == 'V' && parsed[3][2] == 'E' && parsed[3][3] == 'R')
	{
#ifdef __linux
		sprintf(message, "NOTICE %s :%cVERSION %s [%s]%c\r\n", parsed[0], 1, SYSCHAT_QUIT, ctcp_get_distro(), 1);
#else
		sprintf(message, "NOTICE %s :%cVERSION %s [%s]%c\r\n", parsed[0], 1, SYSCHAT_QUIT, os_ver, 1);
#endif
		send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	}
	else if (parsed[3][1] == 'U' && parsed[3][2] == 'S' && parsed[3][3] == 'E')
	{
		sprintf(message, "NOTICE %s :%cUSERINFO %s%c\r\n", parsed[0], 1, SYSCHAT_USERINFO, 1);
		send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	}
	else if (parsed[3][1] == 'C' && parsed[3][2] == 'L' && parsed[3][3] == 'I')
	{
		sprintf(message, "NOTICE %s :%cCLIENTINFO VERSION USERINFO%c\r\n", parsed[0], 1, 1);
		send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	}
}
