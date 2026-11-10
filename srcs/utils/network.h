#ifndef CONNECT_H
# define CONNECT_H

#include "../syschat.h"
#include "error.h"

int network_connect_server(t_syschat *syschat, t_conn *conn, int fatal);
int network_get_ip(t_conn *conn, char *ip);

/*
** Single I/O entry point for the IRC stream, now per connection: the
** socket, the TLS context and the plaintext fallback all live in t_conn,
** otherwise the second server would write on the first server's fd.
*/

/* Bytes written, or -1 (message dropped, already reported by the caller). */
int	net_send(t_conn *conn, const char *buf, size_t len);
/* > 0 bytes read, 0 peer closed, -1 would block (retry on next poll),
** -2 fatal error. */
int	net_recv(t_conn *conn, char *buf, size_t len);
/* 1 when decrypted bytes are already buffered, so poll() must not wait. */
int	net_pending(t_conn *conn);

#endif
