#!/usr/bin/env python3
"""Argument handling checks: no server needed, no network.

Guards the regression that made the documented command crash: the optional
port must be read only when argv[3] exists, and an invalid port must be
refused instead of silently clamped.

Usage: ./build.sh && python3 tests/cli_args.py [binary]
"""
import subprocess, sys, os, pty, termios, time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./syschat"


def run(args, timeout=6):
    proc = subprocess.run([BIN] + args, capture_output=True, text=True, timeout=timeout)
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")




checks = {}


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

# --tls-required means "never plaintext"; combining it with --no-tls is a
# contradiction the client must refuse instead of silently picking a winner.
code, out = run(["--no-tls", "--tls-required", "127.0.0.1", "tester"])
checks["--no-tls + --tls-required -> refused, exit 11"] = code == 11 and "contradict" in out


def starts_without_argument(timeout=8):
    """No argument must start the client (it can /connect now), not print a
    usage error: the old check asserted the opposite behaviour."""
    master, slave = pty.openpty()
    proc = subprocess.Popen([BIN], stdin=slave, stdout=slave, stderr=slave, close_fds=True)
    os.close(slave)
    time.sleep(0.8)
    alive = proc.poll() is None
    os.write(master, b"/exit\n")
    try:
        rc = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        rc = -1
    os.close(master)
    return alive, rc


alive, rc = starts_without_argument()
checks["no argument -> starts disconnected, /exit returns 0"] = alive and rc == 0


def run_pty(args, timeout=10):
    """Fatal connection errors happen after raw mode is entered; the terminal
    must be handed back, otherwise the shell is left without echo."""
    master, slave = pty.openpty()
    before = termios.tcgetattr(slave)
    proc = subprocess.Popen([BIN] + args, stdin=slave, stdout=slave, stderr=slave,
                            close_fds=True)
    rc = proc.wait(timeout=timeout)
    after = termios.tcgetattr(slave)
    os.close(slave)
    os.close(master)
    return (rc, bool(before[3] & termios.ICANON) and bool(before[3] & termios.ECHO),
            bool(after[3] & termios.ICANON), bool(after[3] & termios.ECHO))


rc, was_cooked, is_canon, has_echo = run_pty(["no-such-host.invalid", "tester"])
checks["fatal DNS error restores the terminal (ICANON + ECHO)"] = \
    rc == 4 and was_cooked and is_canon and has_echo

for name, passed in checks.items():
    print(("OK   " if passed else "FAIL ") + name)
sys.exit(0 if all(checks.values()) else 1)
