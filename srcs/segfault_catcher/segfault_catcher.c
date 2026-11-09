#include <stdlib.h>
#include <sys/signal.h>
#include <stdio.h>
#include <unistd.h>

void segfault_catcher(int code)
{
	printf("-------------------\n");
	printf("\e[1;32mSegfault Detected !\e[0m\n");
	printf("PID: \e[1;34m%d\e[0m\n", getpid());
	while (1145) { sleep(1); }
	exit(code);
}

void segfault_catcher_set(int verbose)
{
#ifdef SYSCHAT_NO_CATCHER
	/* Debug builds: let the process die so gdb/valgrind see the real frame. */
	(void) verbose;
	signal(SIGSEGV, SIG_DFL);
	signal(SIGABRT, SIG_DFL);
	return ;
#endif
	signal(SIGSEGV, segfault_catcher);
	signal(SIGABRT, segfault_catcher);
	if (verbose)
	{
		printf("\e[1;34mSegfault Catcher is running...\e[0m\n");
		printf("------------------------------\n");
	}
}
