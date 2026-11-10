#!/usr/bin/env python3
"""Functional check for the cross-server key bindings (mock IRC servers + pty).

Two mock servers on 127.0.0.1:16690 and 127.0.0.1:16691. The client connects
to the first on the command line, the second with /connect, and joins a
different channel on each so the buffer bar itself says which server is on
screen.

Covers:

  * Alt-Tab (ESC Tab) switches server and lands on that server's console
  * Alt-Shift-Tab (ESC ESC [ Z) goes back one server
  * a sequence split across two reads still switches server
  * Ctrl-Tab / Ctrl-Shift-Tab as reported by the kitty keyboard protocol
    (ESC [ 9 ; 5 u / ESC [ 9 ; 6 u)
  * a plain terminal's Ctrl-Tab (the same 0x09 as Tab) stays a BUFFER
    switch -- the documented limitation, not a bug
  * Tab alone never changes server
  * Shift-Tab (ESC [ Z) still walks buffers backwards

Usage:  ./build.sh && python3 tests/functional_server_tabs.py
"""
import os, pty, select, socket, subprocess, sys, termios, time, fcntl, struct, threading

HOST = "127.0.0.1"
PORT_A, PORT_B = 16690, 16691
BIN = os.environ.get("SYSCHAT_BIN", "./syschat")
STATE = {"A": {"log": [], "joins": []}, "B": {"log": [], "joins": []}}


def strip_ansi(a):
    r = []
    k = 0
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
        r.append(a[k])
        k += 1
    return "".join(r)


def serve(srv, name, ready):
    srv.listen(4)
    ready.set()
    while True:
        try:
            conn, _ = srv.accept()
        except OSError:
            return
        threading.Thread(target=session, args=(conn, name), daemon=True).start()


def session(conn, name):
    st = STATE[name]
    buf = b""
    nick = "tester"
    conn.settimeout(0.2)
    deadline = time.time() + 40
    try:
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
                st["log"].append(line)
                parts = line.split(" ")
                cmd = parts[0].upper()
                if cmd == "USER" and len(parts) > 1:
                    nick = parts[1]
                    conn.sendall((":%s 001 %s :Welcome\r\n" % (name, nick)).encode())
                elif cmd == "JOIN":
                    chan = parts[1]
                    st["joins"].append(chan)
                    conn.sendall((":%s!u@h JOIN :%s\r\n" % (nick, chan)).encode())
                    conn.sendall((":%s!u@h PRIVMSG %s :console-%s\r\n"
                                  % (nick, chan, name)).encode())
                elif cmd == "QUIT":
                    return
    finally:
        conn.close()


def main():
    for name, port in (("A", PORT_A), ("B", PORT_B)):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((HOST, port))
        ready = threading.Event()
        threading.Thread(target=serve, args=(srv, name, ready), daemon=True).start()
        ready.wait()

    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 120, 0, 0))
    proc = subprocess.Popen([BIN, HOST, "tester", str(PORT_A)],
                            stdin=slave, stdout=slave, stderr=slave, close_fds=True)
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

    def send_bytes(keys):
        os.write(master, keys)
        pump(0.3)

    def send(keys):
        send_bytes(keys.encode())

    def last_frame():
        raw = out.decode(errors="replace")
        frames = [strip_ansi(f) for f in raw.split("\x1b[H") if strip_ansi(f).strip()]
        return frames[-1] if frames else ""

    def bars():
        rows = last_frame().split("\r\n")
        return (rows[0] if rows else ""), (rows[1] if len(rows) > 1 else "")

    checks = {}
    pump(1.2)

    send("/join #alpha\n")
    pump(0.8)
    checks["server A joined #alpha"] = "#alpha" in STATE["A"]["joins"]

    send("/connect bob two %s %d\n" % (HOST, PORT_B))
    pump(1.2)
    send("/join #beta\n")
    pump(1.2)
    checks["server B joined #beta"] = "#beta" in STATE["B"]["joins"]

    server_row, buffer_row = bars()
    checks["start on server B (buffer bar shows #beta)"] = "#beta" in buffer_row
    checks["server bar lists both servers"] = "two" in server_row and HOST in server_row

    # The server console window is its own tab but not a chip: the buffer bar
    # only lists channels/pms/list windows. So "which server am I on" is read
    # from WHICH channel name the bar holds.
    # --- Alt-Tab: next server ------------------------------------------
    send_bytes(b"\x1b\t")
    pump(0.6)
    server_row, buffer_row = bars()
    checks["Alt-Tab left server B"] = "#beta" not in buffer_row
    checks["Alt-Tab landed on server A"] = "#alpha" in buffer_row

    # --- Alt-Shift-Tab: back one server --------------------------------
    send_bytes(b"\x1b\x1b[Z")
    pump(0.6)
    server_row, buffer_row = bars()
    checks["Alt-Shift-Tab returned to server B"] = "#beta" in buffer_row and \
        "#alpha" not in buffer_row

    # --- sequence split across two reads -------------------------------
    send_bytes(b"\x1b")
    send_bytes(b"\t")
    pump(0.6)
    server_row, buffer_row = bars()
    checks["Alt-Tab split across two reads still switches"] = "#alpha" in buffer_row

    # --- Shift-Tab alone still walks buffers ---------------------------
    send_bytes(b"\x1b[Z")
    pump(0.4)
    server_row, buffer_row = bars()
    checks["Shift-Tab stays on server A"] = "#alpha" in buffer_row

    # --- Tab alone never changes server --------------------------------
    send_bytes(b"\t")
    pump(0.4)
    server_row, buffer_row = bars()
    checks["Tab alone stays on server A"] = "#alpha" in buffer_row

    # --- plain Ctrl-Tab (0x09) is a buffer switch, not a server one ----
    send_bytes(b"\t")
    pump(0.4)
    server_row, buffer_row = bars()
    checks["plain Ctrl-Tab byte stays on server A"] = "#alpha" in buffer_row

    # --- kitty protocol Ctrl-Tab / Ctrl-Shift-Tab ----------------------
    send_bytes(b"\x1b[9;5u")
    pump(0.6)
    server_row, buffer_row = bars()
    checks["kitty Ctrl-Tab switches to server B"] = "#beta" in buffer_row and \
        "#alpha" not in buffer_row

    send_bytes(b"\x1b[9;6u")
    pump(0.6)
    server_row, buffer_row = bars()
    checks["kitty Ctrl-Shift-Tab returns to server A"] = "#alpha" in buffer_row

    # --- xterm modifyOtherKeys Ctrl-Tab --------------------------------
    send_bytes(b"\x1b[27;5;9~")
    pump(0.6)
    server_row, buffer_row = bars()
    checks["modifyOtherKeys Ctrl-Tab switches to server B"] = "#beta" in buffer_row

    # --- arrow keys must not be mistaken for a switch ------------------
    send_bytes(b"\x1b[A")
    pump(0.4)
    server_row, buffer_row = bars()
    checks["arrow key does not switch server"] = "#beta" in buffer_row

    send("/exit\n")
    try:
        checks["clean_exit"] = proc.wait(timeout=5) == 0
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        checks["clean_exit"] = False

    for k, v in checks.items():
        print(("OK   " if v else "FAIL ") + k)
    sys.exit(0 if all(checks.values()) else 1)


main()
