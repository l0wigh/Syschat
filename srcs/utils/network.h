#ifndef CONNECT_H
# define CONNECT_H

#include "../syschat.h"
#include "error.h"

void network_connect_server(t_syschat *syschat);
int network_get_ip(t_syschat *syschat, char *ip);

/*
** Single I/O entry point for the IRC stream. Every caller used to reach
** for send()/recv() on the raw fd; going through these two keeps the TLS
** path (mbedTLS buffers and wants its own write semantics) out of the
** eleven call sites, and makes a lost message a single place to fix.
*/

/* Bytes written, or -1 (message dropped, already reported by the caller). */
int	net_send(t_syschat *syschat, const char *buf, size_t len);
/* > 0 bytes read, 0 peer closed, -1 would block (retry on next poll),
** -2 fatal error. */
int	net_recv(t_syschat *syschat, char *buf, size_t len);
/* 1 when decrypted bytes are already buffered, so poll() must not wait. */
int	net_pending(t_syschat *syschat);

#endif
