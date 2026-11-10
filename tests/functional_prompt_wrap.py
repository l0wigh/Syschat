#!/usr/bin/env python3
"""Wrapped input prompt, end to end: a long line typed in a 40-column terminal
must stay readable (it takes one more row each time it reaches the edge), must
not scroll the screen, and must still be sent whole."""
import os, pty, re, select, socket, subprocess, sys, termios, time, fcntl, struct, threading

HOST, PORT = "127.0.0.1", 16681
COLS, ROWS = 40, 20
log = []


def serve(srv, ready):
    srv.listen(1)
    ready.set()
    conn, _ = srv.accept()
    buf = b""
    conn.settimeout(0.2)
    end = time.time() + 40
    while time.time() < end:
        try:
            data = conn.recv(65536)
        except socket.timeout:
            continue
        if not data:
            break
        buf += data
        while b"\r\n" in buf:
            line, buf = buf.split(b"\r\n", 1)
            log.append(line)
            cmd = line.split(b" ")[0].upper()
            if cmd == b"USER":
                conn.sendall(b":mock 001 tester :Welcome\r\n")
            elif cmd == b"JOIN":
                conn.sendall(b":tester!u@h JOIN :" + line.split(b" ")[1] + b"\r\n")
            elif cmd == b"PRIVMSG":
                text = line.split(b" ", 2)[2][1:]
                conn.sendall(b":tester!u@h PRIVMSG #a :" + text + b"\r\n")
    conn.close()


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
ready = threading.Event()
threading.Thread(target=serve, args=(srv, ready), daemon=True).start()
ready.wait()

m, s = pty.openpty()
fcntl.ioctl(s, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
proc = subprocess.Popen(["./syschat", HOST, "tester", str(PORT)], stdin=s, stdout=s,
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


def send(b):
    os.write(m, b)
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
    """The last complete frame: its rows, ANSI stripped, no trailing newline."""
    raw = bytes(out)
    frames = [f for f in raw.split(b"\x1b[H") if f.strip()]
    if not frames:
        return []
    text = frames[-1].decode("utf-8")          # strict: a split char raises
    rows = text.split("\r\n")
    return [strip(r) for r in rows]


def prompt_rows(rows):
    for i, r in enumerate(rows):
        if "[#a] <tester> " in r or r.startswith(".."):
            return rows[i:]
    return []


marks = {}
pump(1.0)
send(b"/join #a\n")

# --- a 100-character line: wraps, everything still readable ----------------
short = "abecedaire" * 10
send(short.encode())
rows = frame()
p = prompt_rows(rows)
marks["the frame is exactly the terminal height"] = len(rows) == ROWS
marks["the prompt spans several rows"] = len(p) >= 3
marks["the prefix is not repeated on continuation rows"] = (
    sum(r.count("[#a] <tester>") for r in rows) == 1)
marks["the last character typed is visible"] = bool(p) and p[-1].endswith(short[-1])
joined = "".join(r[2:] if i else r for i, r in enumerate(p))
marks["the whole line is visible"] = short in joined
marks["no row is wider than 40 columns"] = all(len(r) <= COLS for r in rows)
marks["the line still arrives whole"] = True        # checked below

send(b"\r")
pump(0.6)
sent = [l for l in log if l.startswith(b"PRIVMSG #a")]
marks["the line still arrives whole"] = any(l.split(b" ", 2)[2][1:] == short.encode()
                                            for l in sent)

# --- a 400-character line: capped, the end stays on screen -----------------
long = "".join(chr(ord("a") + i % 26) for i in range(400))
send(long.encode())
rows = frame()
p = prompt_rows(rows)
marks["the frame stays exactly the terminal height on a paste"] = len(rows) == ROWS
marks["a paste cannot take more than half the screen"] = len(p) <= ROWS // 2
marks["the first visible row says earlier rows were dropped"] = bool(p) and p[0].startswith("..")
tail = "".join(r[2:] if i else r for i, r in enumerate(p))[2:]
marks["the end of the paste is what is on screen"] = long.endswith(tail) and len(tail) > 20
marks["no row is wider than 40 columns on a paste"] = all(len(r) <= COLS for r in rows)
send(b"\x7f" * len(long))          # empty the prompt again
pump(0.4)

# --- accents: wrapping must not split a character --------------------------
accented = "é" * 100
send(accented.encode())
rows = frame()                      # strict utf-8 decode, would raise on a split
p = prompt_rows(rows)
marks["100 accented characters are all displayed"] = len(p) == 4 and "".join(
    r[2:] if i else r for i, r in enumerate(p)).endswith(accented)
marks["no row is wider than 40 columns with accents"] = all(len(r) <= COLS for r in rows)

proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

for k, v in marks.items():
    print(("OK   " if v else "FAIL ") + k)
sys.exit(0 if all(marks.values()) else 1)
