# Syschat 0.4.1-RC1 IRC Client

Syschat is an IRC client made only with syscall and libc + kernel functions.

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

    Usage: ./syschat <host> <nick> [port]

The port is optional and defaults to 6667. It must be a full integer between
1 and 65535: `./syschat irc.libera.chat fr0sted 7000` is valid, `... fr0sted
abc` is refused.

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

Debugging: `SYSCHAT_DEBUG=/tmp/syschat.log ./syschat <host> <nick>` appends
every raw incoming line and the buffer it was routed to. The trace holds the
raw traffic in clear (there is no TLS) — do not leave that file around.

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

Tests (no unit tests, functional checks only):

    tests/run_all.sh                        # offline: cli_args + buffers

    ./build.sh && python3 tests/cli_args.py            # argument handling
    ./build.sh && python3 tests/functional_buffers.py  # mock IRC server + pty
    SYSCHAT_NET_TESTS=1 python3 tests/network_smoke.py # real network, opt-in

`SYSCHAT_NET_TESTS=1` is mandatory for the network test: without it the
script refuses to run, so a `tests/*.py` glob can never open a connection to
a public IRC network.

## Compile flags ?

You can change "-Ofast -flto" for something like "-O2".

The default one works for me, but can have strange behaviors.
