#!/bin/sh
# Single entry point for the functional tests (there are no unit tests).
#
#   tests/run_all.sh            offline tests only
#   SYSCHAT_NET_TESTS=1 tests/run_all.sh   also the real-network smoke test
#
set -e
cd "$(dirname "$0")/.."

./build.sh >/dev/null
echo "--- cli_args ---"
python3 tests/cli_args.py ./syschat
echo "--- functional_buffers ---"
python3 tests/functional_buffers.py

if [ "${SYSCHAT_NET_TESTS:-0}" = "1" ]; then
	echo "--- network_smoke (real network) ---"
	python3 tests/network_smoke.py
else
	echo "--- network_smoke skipped (set SYSCHAT_NET_TESTS=1 to run it) ---"
fi
echo "all tests passed"
