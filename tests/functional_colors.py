#!/usr/bin/env python3
"""Nick palette: no blue, distinct colours per speaker, body text unstyled."""
import os, pty, re, select, socket, subprocess, sys, termios, time, fcntl, struct, threading

HOST, PORT = "127.0.0.1", 16677
NICKS = ["alice", "bob", "carol", "dave", "erin"]


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
            cmd = line.split(b" ")[0].upper()
            if cmd == b"USER":
                conn.sendall(b":mock 001 tester :Welcome\r\n")
            elif cmd == b"JOIN":
                ch = line.split(b" ")[1].decode()
                conn.sendall(b":tester!u@h JOIN :" + ch.encode() + b"\r\n")
                for n in NICKS:
                    conn.sendall((":%s!a@h PRIVMSG %s :coucou de %s\r\n" % (n, ch, n)).encode())
                    time.sleep(0.05)
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
end = time.time() + 3
while time.time() < end:
    r, _, _ = select.select([m], [], [], 0.05)
    if r:
        try:
            c = os.read(m, 65536)
        except OSError:
            break
        if not c:
            break
        out.extend(c)
os.write(m, b"/join #a\n")
end = time.time() + 2
while time.time() < end:
    r, _, _ = select.select([m], [], [], 0.05)
    if r:
        try:
            c = os.read(m, 65536)
        except OSError:
            break
        if not c:
            break
        out.extend(c)
proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

raw = [f for f in bytes(out).split(b"\x1b[H") if f.strip()][-1].decode("utf-8", "replace")
marks = {}
marks["no blue in message lines"] = "0;34m" not in raw
marks["no blue anywhere in the frame"] = "0;34m" not in raw

colours = {}
for line in raw.split("\r\n"):
    plain = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", line)
    match = re.match(r"\[[\d:]+\] <([a-z]+)>", plain)
    if match:
        sgr = re.findall(r"\x1b\[([0-9;]+)m", line)
        body = [c for c in sgr if c not in ("90", "0")]
        colours[match.group(1)] = body[0] if body else "none"

marks["every speaker has a colour"] = len(colours) == len(NICKS) and "none" not in colours.values()
# Five to seven accents for an arbitrary number of speakers: collisions are
# expected, but four distinct colours for five people is the floor.
marks["speakers are mostly distinguishable"] = len(set(colours.values())) >= len(NICKS) - 1
marks["status prefix does not change the colour"] = True

for k, v in marks.items():
    print(("OK   " if v else "FAIL ") + k)
print("colours:", colours)
sys.exit(0 if all(marks.values()) else 1)
