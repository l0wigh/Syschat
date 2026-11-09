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

/* The nickname comes from the network: bound it so the reply always fits in
** BF_SIZE, whatever the sender claims to be called. */
#define CTCP_NICK_MAX (SYSCHAT_NAME_LEN - 1)

void ctcp_handle_request(t_syschat *syschat, char **parsed)
{
	char message[BF_SIZE];

	bzero(message, BF_SIZE);
	if (parsed[3][1] == 'V' && parsed[3][2] == 'E' && parsed[3][3] == 'R')
	{
#ifdef __linux
		snprintf(message, sizeof(message), "NOTICE %.*s :%cVERSION %s [%s]%c\r\n",
			CTCP_NICK_MAX, parsed[0], 1, SYSCHAT_QUIT, ctcp_get_distro(), 1);
#else
		snprintf(message, sizeof(message), "NOTICE %.*s :%cVERSION %s [%s]%c\r\n",
			CTCP_NICK_MAX, parsed[0], 1, SYSCHAT_QUIT, os_ver, 1);
#endif
		send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	}
	else if (parsed[3][1] == 'U' && parsed[3][2] == 'S' && parsed[3][3] == 'E')
	{
		snprintf(message, sizeof(message), "NOTICE %.*s :%cUSERINFO %s%c\r\n",
			CTCP_NICK_MAX, parsed[0], 1, SYSCHAT_USERINFO, 1);
		send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	}
	else if (parsed[3][1] == 'C' && parsed[3][2] == 'L' && parsed[3][3] == 'I')
	{
		snprintf(message, sizeof(message), "NOTICE %.*s :%cCLIENTINFO VERSION USERINFO%c\r\n",
			CTCP_NICK_MAX, parsed[0], 1, 1);
		send(syschat->net_socket, message, strlen(message), MSG_DONTWAIT);
	}
}
