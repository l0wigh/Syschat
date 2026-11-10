#!/bin/sh
# Single entry point for the functional tests and the one unit test.
#
#   tests/run_all.sh            offline tests only
#   SYSCHAT_NET_TESTS=1 tests/run_all.sh   also the real-network smoke test
#
set -e
cd "$(dirname "$0")/.."

./build.sh >/dev/null
echo "--- cli_args (release) ---"
python3 tests/cli_args.py ./syschat

# Same checks on the debug binary: -DSYSCHAT_NO_CATCHER makes a crash exit
# 134/139 instead of looping, and argv handling has already differed there.
./build.sh debug >/dev/null
echo "--- cli_args (debug) ---"
python3 tests/cli_args.py ./syschat_dbg

echo "--- functional_buffers ---"
python3 tests/functional_buffers.py

echo "--- functional_private ---"
python3 tests/functional_private.py

echo "--- functional_numerics ---"
python3 tests/functional_numerics.py

echo "--- functional_list ---"
python3 tests/functional_list.py

echo "--- functional_input ---"
python3 tests/functional_input.py

echo "--- functional_prompt_wrap ---"
python3 tests/functional_prompt_wrap.py

echo "--- functional_colors ---"
python3 tests/functional_colors.py

# Two mock servers on 16680/16681: distinct tabs for the same #chan on two
# networks, per-connection line accumulator, close/exit on every connection.
echo "--- functional_multi ---"
python3 tests/functional_multi.py

# TLS needs no outside network: self-signed certificate generated on the fly,
# local TLS server in Python, local plaintext server for the fallback path.
echo "--- tls_handshake ---"
python3 tests/tls_handshake.py

# wrap_line() is static inside screen.c and only reachable by including the
# translation unit. This covers the wrapping itself (row count, widths, UTF-8,
# indent); the '..' marker is emitted in screen_draw, not in wrap_line, and
# stays verified by manual frames only -- an incoming line is capped at 511
# bytes, so it only triggers on a short or narrow terminal.
echo "--- wrap_unit ---"
${CC:-gcc} -std=c11 -Wall -Wextra -Werror -I. -o tests/wrap_unit tests/wrap_unit.c
./tests/wrap_unit

# The prompt of screen_draw(): faked terminal size, stdout redirected to a
# file, so the frame itself is read back and measured instead of eyeballed.
echo "--- prompt_wrap_unit ---"
${CC:-gcc} -std=c11 -Wall -Wextra -Werror -I. -o tests/prompt_wrap_unit tests/prompt_wrap_unit.c
./tests/prompt_wrap_unit

if [ "${SYSCHAT_NET_TESTS:-0}" = "1" ]; then
	echo "--- network_smoke (real network) ---"
	python3 tests/network_smoke.py
else
	echo "--- network_smoke skipped (set SYSCHAT_NET_TESTS=1 to run it) ---"
fi
echo "all tests passed"
