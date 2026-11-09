#!/usr/bin/env python3
"""Channel listing (/list): mask handling, dedicated buffer, one list at a time.

The 005 lines come from tests/fixtures/libera_005.txt: real output captured
before this host was banned from the network, with the nickname substituted so
a parser hard-coded to that nick fails here.
"""
import os, pty, re, select, socket, subprocess, sys, termios, time, fcntl, struct, threading

HOST, PORT = "127.0.0.1", 16673
NICK = "tester"
FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "libera_005.txt")

CHANNELS = [
    ("#alpha", "42", "le salon alpha"),
    ("#beta", "7", "beta chan"),
    ("#linux-irc", "120", "irc stuff"),
]

log = []
state = {"lists": 0}


def isupport():
    lines = []
    with open(FIXTURE) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            lines.append(line.replace("probe2982", NICK))
    return [l.encode() + b"\r\n" for l in lines]


def serve(srv, ready):
    srv.listen(1)
    ready.set()
    conn, _ = srv.accept()
    buf = b""
    conn.settimeout(0.2)
    end = time.time() + 40
    while time.time() < end:
        try:
            data = conn.recv(4096)
        except socket.timeout:
            continue
        if not data:
            break
        buf += data
        while b"\r\n" in buf:
            line, buf = buf.split(b"\r\n", 1)
            line = line.decode(errors="replace")
            log.append("C>> " + line)
            cmd = line.split(" ")[0].upper()
            if cmd == "USER":
                conn.sendall((":mock 001 %s :Welcome\r\n" % NICK).encode())
                for l in isupport():
                    conn.sendall(l)
            elif cmd == "LIST":
                state["lists"] += 1
                mask = line.split(" ")[1].strip("*") if len(line.split(" ")) > 1 else ""
                conn.sendall(b":mock 321 tester Channel :Users Name\r\n")
                time.sleep(0.8)          # window for a second /list to be refused
                for name, users, topic in CHANNELS:
                    if mask and mask not in name:   # the server does the filtering
                        continue
                    conn.sendall((":mock 322 tester %s %s :%s\r\n" % (name, users, topic)).encode())
                conn.sendall(b":mock 323 tester :End of /LIST\r\n")
    conn.close()


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
ready = threading.Event()
threading.Thread(target=serve, args=(srv, ready), daemon=True).start()
ready.wait()

m, s = pty.openpty()
fcntl.ioctl(s, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
proc = subprocess.Popen(["./syschat", HOST, NICK, str(PORT)], stdin=s, stdout=s,
                        stderr=s, close_fds=True)
os.close(s)
out = bytearray()


def pump(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([m], [], [], 0.05)
        if r:
            try:
                c = os.read(m, 65536)
            except OSError:
                return
            if not c:
                return
            out.extend(c)


def send(k):
    os.write(m, k.encode())
    pump(0.3)


def strip(a):
    r, k = [], 0
    while k < len(a):
        if a[k] == "\033":
            q = k + 1
            if q < len(a) and a[q] == "[":
                q += 1
                while q < len(a) and not ("@" <= a[q] <= "~"):
                    q += 1
                k = q + 1
                continue
            k += 1
            continue
        r.append(a[k]); k += 1
    return "".join(r)


def frame():
    raw = bytes(out)
    frames = [f for f in raw.split(b"\x1b[H") if f.strip()]
    return strip(frames[-1].decode("utf-8", "replace")) if frames else ""


pump(1.0)
marks = {}
send("/list irc\n")          # ELIST=C is in the fixture -> mask must be sent
send("/list again\n")        # must be refused, no second LIST
pump(2.5)
f = frame()
marks["LIST mask sent when the server advertises ELIST=C"] = any(
    l == "C>> LIST *irc*" for l in log)
marks["second /list refused while one is running"] = state["lists"] == 1
marks["refusal is visible"] = "already in progress" in f
marks["channel lines reach the Liste buffer"] = (
    "#linux-irc" in f and "120" in f and "irc stuff" in f)
marks["channels filtered out by the server are absent"] = "#alpha" not in f
marks["footer reports the count"] = re.search(r"\d+ channel\(s\) listed", f) is not None
marks["a new /list works once the first finished"] = True
send("/list\n")
pump(3.0)
marks["a new /list works once the first finished"] = state["lists"] == 2

proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

for k, v in marks.items():
    print(("OK   " if v else "FAIL ") + k)
print("--- last frame ---")
print("\n".join(frame().split("\r\n")[:10]))
sys.exit(0 if all(marks.values()) else 1)
