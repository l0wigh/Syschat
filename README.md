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

Join a channel with /join #channel and start to talk.

Every channel, and every private conversation, gets its own buffer. The top
line shows the buffers: the active one is highlighted, the ones with unread
messages are marked with a `*`.

- `Tab` / `Shift-Tab` : next / previous buffer
- `/next [n]` / `/prev [n]` : same thing from the command line
- `/switch #channel` : jump straight to a buffer (`/lock` still works, as an alias)
- `/close [#channel]` : leave and close a buffer
- `/?` : list the commands

Messages received in a buffer you are not looking at are never dropped: they
are stored in that buffer's history (and flagged unread) until you switch to
it.

If you want to send a pm, use /privmsg <username> :<your text>.

## But why ?

It was at first a pet project, but it works very good for my daily usage of irc and is faster and use almost no ressources.

## How to build ?

    make

or, on a machine without make:

    ./build.sh

## Compile flags ?

You can change "-Ofast -flto" for something like "-O2".

The default one works for me, but can have strange behaviors.
