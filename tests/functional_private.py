#!/usr/bin/env python3
"""Private-message checks: outgoing /msg, /privmsg and incoming PM buffers."""
import os, pty, select, socket, subprocess, sys, termios, time, fcntl, struct, threading

HOST, PORT = "127.0.0.1", 16668
log, state = [], {"private_ok": False}


def serve(srv, ready):
    srv.listen(1)
    ready.set()
    conn, _ = srv.accept()
    buf = b""
    conn.settimeout(0.2)
    deadline = time.time() + 30
    while time.time() < deadline:
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
                conn.sendall(b":mock 001 tester :Welcome\r\n")
            elif cmd == "JOIN":
                chan = line.split(" ")[1]
                conn.sendall((":tester!u@h JOIN :%s\r\n" % chan).encode())
                time.sleep(0.3)
                # incoming private message from a complete stranger
                conn.sendall(b":alice!a@h PRIVMSG tester :salut en prive\r\n")
            elif cmd == "PRIVMSG":
                target = line.split(" ", 2)[1]
                if target == "alice":
                    state["private_ok"] = True
                elif target == "zzz_no_such":
                    conn.sendall(b":mock 401 tester zzz_no_such :No such nick\r\n")
    conn.close()


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
ready = threading.Event()
threading.Thread(target=serve, args=(srv, ready), daemon=True).start()
ready.wait()

master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
proc = subprocess.Popen(["./syschat", HOST, "tester", str(PORT)], stdin=slave,
                        stdout=slave, stderr=slave, close_fds=True)
os.close(slave)
out = bytearray()


def pump(seconds):
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([master], [], [], 0.05)
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
    pump(0.5)


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


pump(1.0)
send("/join #chan\n")
pump(1.5)                      # incoming PM from alice arrives here
marks = {}
marks["incoming PM opens a buffer"] = b"alice" in out
send("/switch alice\n")        # reach the PM buffer with a command
pump(0.4)
send("reponse en prive\n")     # plain text in a PM tab must go to the nick
pump(0.5)
marks["plain text in PM tab goes to the nick"] = any(l == "C>> PRIVMSG alice :reponse en prive" for l in log)
send("/msg alice :bonjour avec msg\n")
pump(0.5)
marks["/msg sends PRIVMSG"] = any(l == "C>> PRIVMSG alice :bonjour avec msg" for l in log)
send("/privmsg alice :bonjour avec privmsg\n")
pump(0.5)
marks["/privmsg sends PRIVMSG"] = any(l == "C>> PRIVMSG alice :bonjour avec privmsg" for l in log)
send("/msg alice deux mots sans deux points\n")
pump(0.5)
marks["/msg without colon keeps the whole text"] = any(
    l == "C>> PRIVMSG alice :deux mots sans deux points" for l in log)
send("/msg zzz_no_such :rate\n")
pump(1.0)
marks["401 error shown in the tab where it was typed"] = "No such nick" in strip(
    out.decode("utf-8", "replace"))
marks["no raw junk sent to server"] = not any(
    l.startswith("C>>  ") or " :alice " in l for l in log)
proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

text = strip(out.decode("utf-8", "replace"))
for k, v in marks.items():
    print(("OK   " if v else "FAIL ") + k)
print("--- server log ---")
for l in log:
    print(l)
sys.exit(0 if all(marks.values()) else 1)
