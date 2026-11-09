#!/bin/sh
# Single entry point for the functional tests (there are no unit tests).
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

if [ "${SYSCHAT_NET_TESTS:-0}" = "1" ]; then
	echo "--- network_smoke (real network) ---"
	python3 tests/network_smoke.py
else
	echo "--- network_smoke skipped (set SYSCHAT_NET_TESTS=1 to run it) ---"
fi
echo "all tests passed"
