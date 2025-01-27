# Syschat 0.2.1 IRC Client

Syschat is an IRC client made only with syscall and libc + kernel functions.

## How it works ?

- termios and fnctl to change stdin mode to non-canonical
- inet to manage connection
- epoll to manage network and stdin events
- write and printf for printing (obviously)
- ansi code to move cursor around

## What it can do ?

- join a channel and talk in it
- send message to server (list, nickserv, ...)
- handle these messages
    - nick
    - part
    - ping

## But why ?

It was at first a pet project, but it works very good for my daily usage of irc and is faster and use almost no ressources.

## Compile flags ?

You can change "-Ofast -flto" for something like "-O2".

The default one works for me, but can have strange behaviors.
