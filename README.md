# Syschat 0.3.1 IRC Client

Syschat is an IRC client made only with syscall and libc + kernel functions.

## How it works ?

- termios and fnctl to change stdin mode to non-canonical
- inet to manage connection
- epoll to manage network and stdin events
- write and printf for printing (obviously)
- ansi codes

## What it can do ?

- join a channel and talk in it
- talk in private message
- send message to server (list, nickserv, ...)
- handle these messages
    - nick
    - part
    - ping
    - kick
    - mode

## How to use

Join a channel and start to talk. If you want to send pm, use /privmsg <username> :<your text>.

If you join multiple channel, you'll see greyed text from them. It means you aren't locked to them. Use /lock <channel> to lock to another channel.

Locking a channel means that your messages will be sent to it and messages from it will be colored.

## But why ?

It was at first a pet project, but it works very good for my daily usage of irc and is faster and use almost no ressources.

## Compile flags ?

You can change "-Ofast -flto" for something like "-O2".

The default one works for me, but can have strange behaviors.
