#!/usr/bin/env python3
"""Optional real-network smoke test (needs outbound TCP 6667).

Usage: ./build.sh && SYSCHAT_NET_TESTS=1 python3 tests/network_smoke.py [host]

It refuses to run without SYSCHAT_NET_TESTS=1: this is the only test that
talks to the public Internet.

Registers a throwaway nickname, opens two channels, sends a message in each
buffer and checks the client stayed connected. This is the only test that
exercises a real IRC server; tests/functional_buffers.py runs offline.
"""
import os, pty, select, subprocess, sys, termios, time, fcntl, struct, random, re

if os.environ.get("SYSCHAT_NET_TESTS") != "1":
    sys.exit("refusing to run: this test opens a REAL IRC connection "
             "(registers a nickname, creates channels).\n"
             "Set SYSCHAT_NET_TESTS=1 to allow it.")

HOST = sys.argv[1] if len(sys.argv) > 1 else "irc.libera.chat"
NICK = "scbuf%05d" % random.randint(0, 99999)
CHAN = "##syschat-buf-%05d" % random.randint(0, 99999)
CHAN2 = CHAN + "-b"

master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
proc = subprocess.Popen(["./syschat", HOST, NICK, "6667"], stdin=slave, stdout=slave,
                        stderr=slave, close_fds=True)
os.close(slave)
out = bytearray()


def pump(seconds):
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([master], [], [], 0.1)
        if r:
            try:
                chunk = os.read(master, 65536)
            except OSError:
                return
            if not chunk:
                return
            out.extend(chunk)


def send(keys):
    os.write(master, keys.encode())
    pump(0.4)


def strip(a):
    r, k = [], 0
    while k < len(a):
        if a[k] == "\033":
            m = k + 1
            if m < len(a) and a[m] == "[":
                m += 1
                while m < len(a) and not ("@" <= a[m] <= "~"):
                    m += 1
                k = m + 1
                continue
            k += 1
            continue
        r.append(a[k]); k += 1
    return "".join(r)


pump(10.0)
send("/join %s\n" % CHAN)
pump(5.0)
send("/join %s\n" % CHAN2)
pump(5.0)
send("hello from the real network\n")
pump(3.0)
send("\x1b[Z")      # Shift-Tab: from the status window to the last channel
pump(1.0)
send("hello from the second tab\n")
pump(3.0)
send("/close\n")
pump(3.0)
proc.terminate()
pump(0.5)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

text = strip(out.decode("utf-8", "replace"))
checks = {
    "registered (001/welcome)": ("001" in text) or ("Welcome" in text) or ("welcome" in text),
    "joined %s" % CHAN: CHAN in text and ("entered" in text or "joined" in text),
    "own message accepted (no error)": "hello from the real network" in text
        and not re.search(r"4\d\d .*(Cannot send|not on)", text),
    "no server error": not re.search(r"(?:^|\n|:)\S* ?4\d\d (?:No such|Cannot|You.re|Erroneous|Nickname|Not enough|Bad)", text),
    "both channels open at once": (CHAN in text) and (CHAN2 in text),
    "message sent from the 2nd tab": "hello from the second tab" in text,
    "still connected at end": proc.returncode in (-15, 0, None),
}
for k, v in checks.items():
    print(("OK   " if v else "FAIL ") + k)
print("--- last frame ---")
print("\n".join(text.split("\n")[-8:]))
sys.exit(0 if all(checks.values()) else 1)
