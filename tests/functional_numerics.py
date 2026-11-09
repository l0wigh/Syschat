#!/usr/bin/env python3
"""Numeric routing: RFC-conformant server numerics, checked by where they land."""
import os, sys, pty, select, socket, subprocess, termios, time, fcntl, struct, threading, re

HOST, PORT = "127.0.0.1", 16672
SYNC = [
    b":mock 331 tester #alpha :No topic is set\r\n",
    b":mock 332 tester #alpha :Le topic du salon\r\n",
    b":mock 333 tester #alpha alice!a@h 1700000000\r\n",
    b":mock 324 tester #alpha +nt\r\n",
    b":mock 366 tester #alpha :End of /NAMES list.\r\n",
    b":mock 353 tester = #alpha :@alice bob\r\n",
    b":mock 421 tester FOO :Unknown command\r\n",
    b":mock 401 tester zzz_no_such :No such nick\r\n",
]


def serve(srv, ready):
    srv.listen(1)
    ready.set()
    conn, _ = srv.accept()
    buf = b""
    conn.settimeout(0.2)
    end = time.time() + 20
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
            cmd = line.decode(errors="replace").split(" ")[0].upper()
            if cmd == "USER":
                conn.sendall(b":mock 001 tester :Welcome\r\n")
            elif cmd == "JOIN":
                conn.sendall(b":tester!u@h JOIN :#alpha\r\n")
                time.sleep(0.3)
                for l in SYNC:
                    conn.sendall(l)
            elif cmd == "PRIVMSG":
                conn.sendall(b":mock 401 tester zzz_no_such :No such nick\r\n")
    conn.close()


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
ready = threading.Event()
threading.Thread(target=serve, args=(srv, ready), daemon=True).start()
ready.wait()

m, s = pty.openpty()
fcntl.ioctl(s, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
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


def send(k):
    os.write(m, k.encode())
    pump(0.4)


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


def last_frame():
    raw = bytes(out)
    frames = [f for f in raw.split(b"\x1b[H") if f.strip()]
    return strip(frames[-1].decode("utf-8", "replace")) if frames else ""


pump(1.0)
send("/join #alpha\n")
pump(2.0)
marks = {}
frame = last_frame()
marks["topic shown in the channel tab"] = "Le topic du salon" in frame
marks["topic is labelled, not raw"] = "Topic: Le topic du salon" in frame
marks["no-topic shown in the channel tab"] = "No topic is set" in frame
marks["NAMES list shown in the channel tab"] = "Users: @alice bob" in frame
marks["modes labelled"] = "Modes: +nt" in frame
marks["topic setter labelled"] = "Topic set by alice" in frame
marks["no top-level target prefix left"] = "#alpha :Le topic" not in frame
marks["unknown command stays in the server tab"] = "Unknown command" not in frame

send("/msg zzz_no_such :rate\n")
pump(1.5)
frame = last_frame()
marks["401 shown where the user typed"] = "No such nick" in frame

send("/switch #alpha\n")
pump(0.8)
frame = last_frame()
marks["channel tab still clean after coming back"] = "Le topic du salon" in frame

proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

for k, v in marks.items():
    print(("OK   " if v else "FAIL ") + k)
print("--- dernier écran ---")
print("\n".join(frame.split("\r\n")[:8]))
print("--- onglet serveur (421) ---")
send_frame = strip(bytes(out).split(b"\x1b[H")[-1].decode("utf-8", "replace"))
print(send_frame.split("\r\n")[0] if send_frame else "")
sys.exit(0 if all(marks.values()) else 1)
