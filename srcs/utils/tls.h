#ifndef TLS_H
# define TLS_H

#include "../syschat.h"

/*
** TLS is optional and PER CONNECTION: nothing here is called unless
** conn->tls is set. All mbedTLS structures stay opaque to the rest of the
** codebase (only tls.c includes the mbedTLS headers), so the client still
** compiles and behaves exactly as before when TLS is off.
*/

/* Returns 0 on a successful verified handshake, -1 otherwise and fills
** `err` with a printable reason. On failure the socket is left open and
** in its pre-TLS state (blocking), ready for the plaintext fallback. */
int		tls_connect(t_conn *conn, char *err, size_t errlen);
int		tls_send(t_conn *conn, const char *buf, size_t len);
/* > 0 bytes read, 0 peer closed, -1 would block, -2 fatal error. */
int		tls_recv(t_conn *conn, char *buf, size_t len);
/* "plaintext" or "TLSv1.3, TLS1-3-... , cert verified", for the banner. */
void	tls_summary(t_conn *conn, char *buf, size_t len);
/* 1 when mbedTLS still holds decrypted bytes, so poll() must not wait. */
int		tls_pending(t_conn *conn);
void	tls_close(t_conn *conn);

#endif
