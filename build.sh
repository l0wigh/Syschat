#!/bin/sh
# Build without make (this machine has no make).
# Same source list and same flags as the Makefile.
set -e
cd "$(dirname "$0")"
CC="${CC:-gcc}"
CC_FLAGS="-std=c11 -Wall -Werror -Wextra -Wno-unused-result -Wno-unused-variable -O2"

SRCS="srcs/syschat.c
srcs/utils/network.c
srcs/utils/error.c
srcs/utils/commands.c
srcs/utils/server.c
srcs/utils/buffers.c
srcs/utils/screen.c
srcs/utils/ctcp.c
srcs/segfault_catcher/segfault_catcher.c"

echo "[BUILD] $CC $CC_FLAGS"
$CC $CC_FLAGS -o syschat $SRCS
echo "[END] syschat"
