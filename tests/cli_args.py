#!/usr/bin/env python3
"""Argument handling checks: no server needed, no network.

Guards the regression that made the documented command crash: the optional
port must be read only when argv[3] exists, and an invalid port must be
refused instead of silently clamped.

Usage: ./build.sh && python3 tests/cli_args.py [binary]
"""
import subprocess, sys, os

BIN = sys.argv[1] if len(sys.argv) > 1 else "./syschat"


def run(args, timeout=6):
    proc = subprocess.run([BIN] + args, capture_output=True, text=True, timeout=timeout)
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")




checks = {}

code, out = run([])
checks["no argument -> usage, exit 1"] = code == 1 and "hostname" in out

code, out = run(["example.invalid"])
checks["host only -> usage, exit 1"] = code == 1 and "hostname" in out

code, out = run(["127.0.0.1", "tester", "abc"])
checks["port 'abc' -> refused, exit 9"] = code == 9 and "65535" in out

code, out = run(["127.0.0.1", "tester", "99999"])
checks["port 99999 -> refused, exit 9"] = code == 9 and "65535" in out

code, out = run(["127.0.0.1", "tester", "-1"])
checks["port -1 -> refused, exit 9"] = code == 9 and "65535" in out

# No port: the path that used to abort. An unresolvable host makes the check
# hermetic -- no dependency on a local server listening on 6667 -- and the
# crash happened in argv parsing, before any lookup: exit 4 means we got past it.
code, out = run(["no-such-host.invalid", "tester"], timeout=10)
checks["host nick (no port) -> parses, then fails on DNS (4)"] = code == 4

for name, passed in checks.items():
    print(("OK   " if passed else "FAIL ") + name)
sys.exit(0 if all(checks.values()) else 1)
