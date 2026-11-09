#!/usr/bin/env python3
"""Input path: accents reach the client, backspace removes a whole character,
and long messages are cut on the server limit without breaking UTF-8."""
import os, pty, re, select, socket, subprocess, sys, termios, time, fcntl, struct, threading

HOST, PORT = "127.0.0.1", 16676
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
            if line.split(b" ")[0].upper() == b"USER":
                conn.sendall(b":mock 001 tester :Welcome\r\n")
            elif line.split(b" ")[0].upper() == b"JOIN":
                conn.sendall(b":tester!u@h JOIN :" + line.split(b" ")[1] + b"\r\n")
    conn.close()


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
ready = threading.Event()
threading.Thread(target=serve, args=(srv, ready), daemon=True).start()
ready.wait()

m, s = pty.openpty()
fcntl.ioctl(s, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
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
    raw = bytes(out)
    frames = [f for f in raw.split(b"\x1b[H") if f.strip()]
    return strip(frames[-1].decode("utf-8", "replace")) if frames else ""


pump(1.0)
send(b"/join #a\n")
marks = {}

send("héllo".encode())
f = frame()
marks["accent typed shows up in the prompt"] = "héllo" in f
marks["euro sign accepted"] = True
send(" €".encode())
f = frame()
marks["euro sign accepted"] = "€" in f

send(b"\x7f")                      # one backspace: drops the whole €
f = frame()
marks["backspace removes a whole character"] = "€" not in f and "héllo" in f

send(b"\r")
send(("/msg #a " + "é" * 300 + "\n").encode())
pump(0.8)
f = frame()
sent = [l for l in log if l.startswith(b"PRIVMSG #a")]
longest = max((len(l) for l in sent), default=0)
marks["long message truncated to the 512 byte limit"] = 0 < longest <= 510
try:
    for l in sent:
        l.decode("utf-8")          # raises on a half character
    marks["truncation keeps whole characters"] = bool(sent)
except UnicodeDecodeError:
    marks["truncation keeps whole characters"] = False
marks["truncation is announced to the user"] = "message tronqué" in f

send(("x" * 1500).encode())       # a paste longer than the input buffer
send(b"\x7f")
f = frame()
marks["over-long paste is announced, not silently cut"] = "input full" in f

proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

for k, v in marks.items():
    print(("OK   " if v else "FAIL ") + k)
print("--- longest PRIVMSG: %d octets ---" % longest)
sys.exit(0 if all(marks.values()) else 1)
