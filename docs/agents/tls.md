# TLS and exit codes

Source of truth: `srcs/syschat.c` (argv/env), `srcs/utils/network.c` (choice and fallback), `srcs/utils/tls.c` (handshake), `srcs/utils/error.c` (messages).

## Decision order
1. Default: TLS on only if port == 6697.
2. `--tls` / `SYSCHAT_TLS=1` forces on. `--no-tls` / `SYSCHAT_TLS=0` forces off.
3. `--insecure` / `SYSCHAT_TLS_NO_VERIFY=1`: encrypt, skip certificate verification. Orthogonal to strict.
4. `--tls-required` / `SYSCHAT_TLS_STRICT=1`: a failed handshake is fatal (exit 10), reason to stderr. Never plaintext.
5. `--tls-required` together with `--no-tls` is refused at startup, exit 11.

## Fallback
Without strict: a failed handshake reconnects in plaintext and prints in red that the session is not encrypted. Syschat does not exit.

## Certificate bundle
Checked in order: `/etc/ssl/certs/ca-certificates.crt`, `/etc/ssl/cert.pem`, `/usr/local/etc/openssl/cert.pem`. If none exists and `--insecure` is not set, the handshake is refused (no trust-anything fallback).

## Exit codes (verified in source)
- 1: missing `<hostname> <nickname>`.
- 9: invalid port (must be integer 1..65535).
- 10: TLS required and handshake failed.
- 11: `--tls-required` with `--no-tls`.
- 4..7: DNS / socket / address / connect errors (see `error_print`).

## Tests
`tests/tls_handshake.py` — self-signed cert via `openssl` CLI, local TLS and plaintext servers, drives `./syschat` in a pty. Offline.
