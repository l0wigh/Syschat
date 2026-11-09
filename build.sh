#!/bin/sh
# Build without make (this machine has no make).
# Same source list and same flags as the Makefile.
#
#   ./build.sh          release (-O2)
#   ./build.sh debug    -g3 -O0, no segfault handler, symbols for gdb/valgrind
#   ./build.sh ubsan    -g3 -O1 -fsanitize=undefined
#   ./build.sh clean    drop the vendored mbedTLS objects
#
# Notes on this toolchain (zig cc, musl target):
#   - ASan is NOT usable: -fsanitize=address compiles but fails to link
#     (__asan_init undefined, no compiler-rt, no libasan on disk).
#   - UBSan is usable but does not behave like upstream: on the first
#     undefined behaviour it panics, prints file:line and exits with 134.
#     That is not a syschat crash, it is the sanitiser stopping the process.
#   - That runtime emits no __ubsan_handle_* symbols, so `nm | grep ubsan`
#     proves nothing here: trigger a real UB to check the instrumentation.
#
# TLS: mbedTLS is vendored in vendor/mbedtls and built with the same compiler
# as syschat, so there is no system libssl to install and no libc mismatch.
# Its objects live in build/mbedtls/ and are reused by every variant;
# third-party code is built without -Werror and without sanitizers.
set -e
cd "$(dirname "$0")"
CC="${CC:-gcc}"
CC_FLAGS="-std=c11 -Wall -Werror -Wextra -Wno-unused-result -Wno-unused-variable -O2"
NAME=syschat

MBEDTLS_DIR=vendor/mbedtls
MBEDTLS_OBJ=build/mbedtls
MBEDTLS_INC="-I$MBEDTLS_DIR/include -I$MBEDTLS_DIR/library"
MBEDTLS_CFLAGS="-std=c11 -O2 -w $MBEDTLS_INC"

case "${1:-release}" in
	release) ;;
	debug)  CC_FLAGS="-std=c11 -Wall -Wextra -g3 -O0 -Wno-unused-result -DSYSCHAT_NO_CATCHER"
	        NAME=syschat_dbg ;;
	ubsan)  CC_FLAGS="-std=c11 -Wall -Wextra -g3 -O1 -Wno-unused-result -DSYSCHAT_NO_CATCHER -fsanitize=undefined"
	        NAME=syschat_ubsan ;;
	clean)  rm -rf build
	        echo "[CLEAN] build/"
	        exit 0 ;;
	*)      echo "usage: $0 [release|debug|ubsan|clean]" >&2; exit 2 ;;
esac

SRCS="srcs/syschat.c
srcs/utils/network.c
srcs/utils/tls.c
srcs/utils/error.c
srcs/utils/commands.c
srcs/utils/server.c
srcs/utils/buffers.c
srcs/utils/screen.c
srcs/utils/ctcp.c
srcs/segfault_catcher/segfault_catcher.c"

build_mbedtls()
{
	mkdir -p "$MBEDTLS_OBJ"
	for src in "$MBEDTLS_DIR"/library/*.c; do
		obj="$MBEDTLS_OBJ/$(basename "$src" .c).o"
		if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
			$CC $MBEDTLS_CFLAGS -c "$src" -o "$obj"
		fi
	done
}

echo "[BUILD] mbedTLS (vendor) -> $MBEDTLS_OBJ"
build_mbedtls

echo "[BUILD] $CC $CC_FLAGS -> $NAME"
$CC $CC_FLAGS $MBEDTLS_INC -o $NAME $SRCS $MBEDTLS_OBJ/*.o
echo "[END] $NAME"
