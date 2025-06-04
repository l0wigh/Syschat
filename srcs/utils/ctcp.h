#ifndef CTCP_H
# define CTCP_H

#include "../syschat.h"

#if defined (__WIN32) || defined (__WIN64)
	static const char *os_ver = "Windows";
#else
#ifdef __linux
	static const char *os_ver = "Linux";
#else
	static const char *os_ver = "Confidential NSA OS";
#endif
#endif

void ctcp_handle_request(t_syschat *syschat, char **parsed);

#endif // !CTCP_H

