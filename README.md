# Syschat 0.4.2-RC1 IRC Client

Syschat is an IRC client made with syscall and libc + kernel functions. The
one exception is TLS: mbedTLS is vendored in `vendor/mbedtls` and built with
the same compiler, so there is still nothing to install on the system and the
binary stays self-contained. With TLS off, the client only uses libc.

## How it works ?

- termios and fnctl to change stdin mode to non-canonical
- inet to manage connection
- poll to manage network and stdin events
- write and printf for printing (obviously)
- ansi codes

## What it can do ?

- join a channel and talk in it
- talk in private message
- keep several channels open at once, one buffer (tab) per channel
- send message to server (list, nickserv, ...)
- handle these messages
    - nick
    - part
    - ping
    - kick
    - mode

## How to use

    Usage: ./syschat <host> <nick> [port] [--tls] [--no-tls] [--insecure] [--tls-required]

The port is optional and defaults to 6667. It must be a full integer between
1 and 65535: `./syschat irc.libera.chat fr0sted 7000` is valid, `... fr0sted
abc` is refused.

The command line is otherwise unchanged: no flag is needed for the previous
behaviour, and old commands keep working as before.

## TLS

TLS is **off by default**, as it always was, except when the port is 6697:
that port means TLS on every network worth the name, so
`./syschat irc.libera.chat nick 6697` is encrypted without any flag.

| flag / variable | effect |
| --- | --- |
| `--tls` / `-tls` | force TLS on any port (`./syschat host nick 6667 --tls`) |
| `--no-tls` / `-no-tls` | force plaintext, even on 6697 |
| `--insecure` / `-insecure` | TLS without certificate verification (self-signed bouncer) |
| `--tls-required` / `-tls-required` | strict: never fall back to plaintext |
| `SYSCHAT_TLS=1` / `=0` | same as `--tls` / `--no-tls` |
| `SYSCHAT_TLS_NO_VERIFY=1` | same as `--insecure` |
| `SYSCHAT_TLS_STRICT=1` | same as `--tls-required` |

The first line of the server buffer tells you what you got, e.g.
`Connected (TLSv1.3, TLS1-3-CHACHA20-POLY1305-SHA256, cert verified)` or
`Connected (plaintext, no encryption)`. There is no silent encryption.

Certificates are checked against the system bundle
(`/etc/ssl/certs/ca-certificates.crt`, `/etc/ssl/cert.pem`,
`/usr/local/etc/openssl/cert.pem`). If none is present and `--insecure` was
not given, the handshake is refused rather than downgraded to "trust anything".

If the handshake fails for any reason (port is plaintext, certificate
rejected, server speaks another protocol), syschat **does not exit**: it
reconnects in plaintext and prints, in red, that the session is not
encrypted. You can still connect to a server that does not do TLS.

### Strict mode

That fallback is a convenience, but it is also a free downgrade for anyone
able to cut the handshake. `--tls-required` (`SYSCHAT_TLS_STRICT=1`) removes
it: the handshake failure becomes fatal, the reason goes to stderr, and the
exit code is **10**. Nothing is ever sent in clear.

- `--tls-required` and `--insecure` are orthogonal: strict is "never
  plaintext", not "verify everything". A self-signed bouncer works with both.
- `--tls-required` with `--no-tls` is a contradiction and is refused at
  startup with exit code **11**, rather than silently picking a winner.
- "Not being blocked" stays available through `--no-tls` (a deliberate
  choice) instead of through an accidental downgrade.

Join a channel with /join #channel and start to talk.

Every channel, and every private conversation, gets its own buffer. The top
line shows the buffers: the active one is highlighted, the ones with unread
messages are marked with a `*`.

- `Tab` / `Shift-Tab` : next / previous buffer
- `/next [n]` / `/prev [n]` : same thing from the command line
- `/switch #channel` : jump straight to a buffer (`/lock` still works, as an alias)
- `/close [#channel]` : leave and close a buffer
- `/?` : list the commands

Debug builds: `./build.sh debug` produces `syschat_dbg` (`-g3 -O0`, symbols,
segfault handler disabled so gdb/valgrind stop on the real frame and a core
can be written). `./build.sh ubsan` adds `-fsanitize=undefined`; on this
toolchain the first undefined behaviour **panics and exits with 134** instead
of warning and continuing — that is the sanitiser, not a syschat crash.

If a line cannot be written (socket stuck for 200 ms, peer gone), the drop is
reported in the active buffer in red — previously it was a silent `-1` that
every caller ignored.

Debugging: `SYSCHAT_DEBUG=/tmp/syschat.log ./syschat <host> <nick>` appends
every raw incoming line and the buffer it was routed to. The trace holds the
decrypted traffic in clear, including when TLS is on — do not leave that file
around.

The first buffer is the server window: registration and MOTD lines, server
notices and errors (numerics) are printed there.

Messages received in a buffer you are not looking at are never dropped: they
are stored in that buffer's history (and flagged unread) until you switch to
it.

If you want to send a pm, use /privmsg <username> :<your text>.

## But why ?

It was at first a pet project, but it works very good for my daily usage of irc and is faster and use almost no ressources.

## How to build ?

    make

or, on a machine without make (the only target exercised so far):

    ./build.sh

The vendored mbedTLS objects are built once into `build/mbedtls/` (about 30s
the first time) and reused by the three variants; `./build.sh clean` drops
them. The Makefile does the same thing under `build/mbedtls/`.

Tests (unit + functional):

    tests/run_all.sh                        # offline: release + debug + buffers

    ./build.sh && python3 tests/cli_args.py            # argument handling
    ./build.sh && python3 tests/functional_buffers.py  # mock IRC server + pty
    ./build.sh && python3 tests/functional_private.py  # private messages
    ./build.sh && python3 tests/functional_numerics.py # server numerics
    ./build.sh && python3 tests/functional_list.py     # /list
    ./build.sh && python3 tests/functional_input.py    # UTF-8 input, 512-byte limit
    ./build.sh && python3 tests/functional_prompt_wrap.py # wrapped prompt, 40-column pty
    ./build.sh && python3 tests/functional_colors.py   # nick palette
    ./build.sh && python3 tests/tls_handshake.py       # TLS, fallback, port defaults
    SYSCHAT_NET_TESTS=1 python3 tests/network_smoke.py # real network, opt-in

`SYSCHAT_NET_TESTS=1` is mandatory for the network test: without it the
script refuses to run, so a `tests/*.py` glob can never open a connection to
a public IRC network.

## Compile flags ?

You can change "-Ofast -flto" for something like "-O2".

The default one works for me, but can have strange behaviors.
