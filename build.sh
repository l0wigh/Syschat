#!/bin/sh
# Build without make (this machine has no make).
# Same source list and same flags as the Makefile.
#
#   ./build.sh          release (-O2)
#   ./build.sh debug    -g3 -O0, no segfault handler, symbols for gdb/valgrind
#   ./build.sh ubsan    -g3 -O1 -fsanitize=undefined (ASan is not available
#                       with this toolchain: it fails to link)
set -e
cd "$(dirname "$0")"
CC="${CC:-gcc}"
CC_FLAGS="-std=c11 -Wall -Werror -Wextra -Wno-unused-result -Wno-unused-variable -O2"
NAME=syschat

case "${1:-release}" in
	release) ;;
	debug)  CC_FLAGS="-std=c11 -Wall -Wextra -g3 -O0 -Wno-unused-result -DSYSCHAT_NO_CATCHER"
	        NAME=syschat_dbg ;;
	ubsan)  CC_FLAGS="-std=c11 -Wall -Wextra -g3 -O1 -Wno-unused-result -DSYSCHAT_NO_CATCHER -fsanitize=undefined"
	        NAME=syschat_ubsan ;;
	*)      echo "usage: $0 [release|debug|ubsan]" >&2; exit 2 ;;
esac

SRCS="srcs/syschat.c
srcs/utils/network.c
srcs/utils/error.c
srcs/utils/commands.c
srcs/utils/server.c
srcs/utils/buffers.c
srcs/utils/screen.c
srcs/utils/ctcp.c
srcs/segfault_catcher/segfault_catcher.c"

echo "[BUILD] $CC $CC_FLAGS -> $NAME"
$CC $CC_FLAGS -o $NAME $SRCS
echo "[END] $NAME"
