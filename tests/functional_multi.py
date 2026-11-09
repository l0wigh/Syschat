#!/usr/bin/env python3
"""Functional check for multi-server support (mock IRC servers + pty).

Two independent mock servers listen on 127.0.0.1:16680 and 127.0.0.1:16681.
The client connects to the first on the command line, the second with
/connect. The scenario covers:

  * two servers both on "#chan": two distinct tabs, shown as label/#chan
  * messages from each server land in their own tab
  * half a line from server A followed by a full line from server B is NOT
    glued into one IRC line (the per-connection accumulator)
  * /close <label> drops the connection and its buffers, the other server
    keeps working
  * typing in a server console window sends nothing at all
  * /exit sends QUIT to every connection and exits 0

Usage:  make && python3 tests/functional_multi.py

SYSCHAT_BIN overrides the client binary (default ./syschat) so the scenario
can also be replayed against a build of an older commit to archive the
red run, without copying this file into another tree.
"""
import os, pty, re, select, socket, subprocess, sys, termios, time, fcntl, struct, threading

HOST = "127.0.0.1"
PORT_A, PORT_B = 16680, 16681
BIN = os.environ.get("SYSCHAT_BIN", "./syschat")
STATE = {
    "A": {"log": [], "joins": [], "quit": False},
    "B": {"log": [], "joins": [], "quit": False},
}


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
    """One mock server; each accepted connection is served on its own thread
    so a second /connect after a /close is still answered."""
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
    st["conn"] = conn
    buf = b""
    nick = "tester"
    conn.settimeout(0.2)
    deadline = time.time() + 40
    try:
        while time.time() < deadline:
            if st.get("inject"):
                try:
                    conn.sendall(st["inject"].encode())
                except OSError:
                    break
                st["inject"] = None
                continue
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
                    if chan == "#chan" and not st.get("joined_once"):
                        st["joined_once"] = True
                elif cmd == "PART" and len(parts) > 1:
                    conn.sendall((":%s!u@h PART %s\r\n" % (nick, parts[1])).encode())
                elif cmd == "QUIT":
                    st["quit"] = True
    finally:
        conn.close()


def main():
    servers = {}
    for name, port in (("A", PORT_A), ("B", PORT_B)):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((HOST, port))
        ready = threading.Event()
        threading.Thread(target=serve, args=(srv, name, ready), daemon=True).start()
        ready.wait()
        servers[name] = srv

    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
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

    def send(keys):
        os.write(master, keys.encode())
        pump(0.25)

    def plain():
        return strip_ansi(out.decode(errors="replace"))

    def frames():
        raw = out.decode(errors="replace")
        return [strip_ansi(f) for f in raw.split("\x1b[H") if strip_ansi(f).strip()]

    def last_frame():
        fs = frames()
        return fs[-1] if fs else ""

    checks = {}
    A_LABEL = HOST           # the server from the command line is named by its host
    pump(1.2)

    # --- first server: join before /connect, which moves the focus -----
    send("/join #chan\n")
    pump(0.6)

    # --- second server ------------------------------------------------
    send("/connect bob two %s %d\n" % (HOST, PORT_B))
    pump(1.0)
    checks["second server connected"] = any(l.startswith("USER") for l in STATE["B"]["log"])
    checks["the nickname given to /connect is the one used"] = \
        any(l.startswith("NICK bob") for l in STATE["B"]["log"])

    # /connect moved the focus to the new server's own window
    send("/join #chan\n")
    pump(0.6)
    checks["joined #chan on both servers"] = "#chan" in STATE["A"]["joins"] \
        and "#chan" in STATE["B"]["joins"]

    # Two bars: the servers on top, then the buffers of the CURRENT server
    # only. The buffer bar prints the bare name -- scoped to one server it
    # cannot collide -- so the proof that the two "#chan" stay apart is that
    # exactly one of them is on screen at a time, and the server bar carries
    # both labels.
    rows = last_frame().split("\r\n")
    server_row, buffer_row = rows[0], rows[1] if len(rows) > 1 else ""
    checks["server bar lists both servers"] = A_LABEL in server_row and "two" in server_row
    checks["buffer bar shows only the active server's buffers"] = \
        buffer_row.count("#chan") == 1

    # --- routing ------------------------------------------------------
    def inject(name, line):
        STATE[name]["inject"] = line
        time.sleep(0.4)

    inject("A", ":alice!a@h PRIVMSG #chan :from-server-one\r\n")
    pump(0.6)
    send("/switch %s/#chan\n" % A_LABEL)
    pump(0.5)
    checks["message from server one lands in its own tab"] = \
        "from-server-one" in plain()[-5000:]

    inject("B", ":bob!b@h PRIVMSG #chan :from-server-two\r\n")
    pump(0.6)
    send("/switch two/#chan\n")
    pump(0.5)
    checks["message from server two lands in its own tab"] = \
        "from-server-two" in plain()[-5000:]
    checks["server one message did not leak into server two tab"] = \
        "from-server-one" not in last_frame()

    # --- per-connection line accumulator ------------------------------
    inject("A", ":alice!a@h PRIVMSG #chan :half-")
    inject("B", ":bob!b@h PRIVMSG #chan :BONLY\r\n")
    inject("A", "REST\r\n")
    pump(0.8)
    send("/switch two/#chan\n")
    pump(0.4)
    b_tail = plain()[-4000:]
    send("/switch %s/#chan\n" % A_LABEL)
    pump(0.4)
    a_tail = plain()[-4000:]
    checks["half line from A completed with its own tail"] = "half-REST" in a_tail
    checks["line from B stayed whole"] = "BONLY" in b_tail
    checks["lines from two servers were not glued"] = "BONLY-REST" not in plain() \
        and "half-BONLY" not in plain()

    # --- close one server --------------------------------------------
    send("/close two\n")
    pump(0.7)
    rows = last_frame().split("\r\n")
    server_row = rows[0]
    checks["closing a server removes it from the server bar"] = "two" not in server_row
    checks["the other server is still visible"] = A_LABEL in server_row
    checks["the surviving server keeps its buffer bar"] = "#chan" in rows[1]

    inject("A", ":alice!a@h PRIVMSG #chan :still-alive-after-close\r\n")
    pump(0.6)
    checks["surviving server still delivers"] = "still-alive-after-close" in plain()[-4000:]

    # --- typing in a server window must not send anything -------------
    send("/server %s\n" % A_LABEL)
    pump(0.3)
    before = len(STATE["A"]["log"])
    send("this must not be sent\n")
    pump(0.6)
    new = STATE["A"]["log"][before:]
    checks["no PRIVMSG from a server console window"] = not any("PRIVMSG" in l for l in new)

    # --- /connect without a port must mean 6697 and TLS ---------------
    # A bare TCP socket on 6697: the first connection gets a ClientHello
    # (so TLS is attempted), the handshake cannot complete, and the client
    # falls back to plaintext on a second connection -- which must be
    # announced, and must not be labelled "TLS".
    tls_probe = socket.socket()
    tls_probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tls_probe.bind((HOST, 6697))
    tls_probe.listen(2)
    seen = {}

    def serve_tls_probe():
        try:
            first, _ = tls_probe.accept()
            first.settimeout(2.0)
            seen["hello"] = first.recv(8)
            first.close()
            second, _ = tls_probe.accept()
            second.settimeout(2.0)
            buf = b""
            while b"USER" not in buf:
                chunk = second.recv(4096)
                if not chunk:
                    break
                buf += chunk
            nick = "bob"
            for line in buf.split(b"\r\n"):
                if line.startswith(b"USER"):
                    nick = line.split(b" ")[1].decode()
            second.sendall((":m 001 %s :Welcome\r\n" % nick).encode())
            seen["plain"] = second      # kept open: the checks run on it
        except OSError:
            pass
        finally:
            tls_probe.close()

    threading.Thread(target=serve_tls_probe, daemon=True).start()
    send("/connect bob three %s\n" % HOST)     # no port at all
    pump(2.5)
    checks["no port defaults to 6697 over TLS"] = seen.get("hello", b"")[:2] == b"\x16\x03"
    frame = last_frame()
    checks["a plaintext fallback is announced"] = "NOT encrypted" in frame
    checks["a plaintext session is not labelled TLS"] = "127.0.0.1:6697, TLS" not in frame
    send("/disconnect three\n")
    pump(0.5)
    try:
        seen["plain"].close()
    except Exception:
        pass

    # --- a /close must not be undone by the server's PART echo --------
    send("/server %s\n" % A_LABEL)
    pump(0.3)
    send("/join #gone\n")
    pump(0.6)
    # Line 0 is the servers bar and never holds a channel name: reading it
    # made this assertion vacuous. The buffer bar is line 1, and the chip has
    # to be there BEFORE the /close for its absence to mean anything.
    def buffer_chips():
        rows = last_frame().split("\r\n")
        if len(rows) < 2:
            return []
        return [t for t in rows[1].split() if t.startswith("#")]

    before_chips = buffer_chips()
    send("/close #gone\n")
    pump(1.2)                   # the mock echoes our own PART back
    after_chips = buffer_chips()
    checks["closed channel is not resurrected by its PART echo"] = \
        "#gone" in before_chips and "#gone" not in after_chips \
        and len(after_chips) == len(before_chips) - 1
    send("/server %s\n" % A_LABEL)
    pump(0.4)
    checks["the echo is reported in the server window"] = "you left #gone" in last_frame()

    # --- a server that drops must explain itself somewhere visible -----
    send("/connect bob two %s %d\n" % (HOST, PORT_B))
    pump(1.0)
    # guard: on a baseline without /connect there is no socket to hang up
    if STATE["B"].get("conn"):
        STATE["B"]["conn"].close()      # the server hangs up, not the user
    pump(1.2)
    send("/server %s\n" % A_LABEL)
    pump(0.6)
    checks["client survives a server drop"] = proc.poll() is None
    checks["drop notice lands in a surviving console"] = \
        "-- two disconnected" in last_frame()

    # --- /exit says goodbye on every connection -----------------------
    send("/connect bob two %s %d\n" % (HOST, PORT_B))
    pump(1.0)
    send("/exit\n")
    try:
        rc = proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        rc = -1
    end = time.time() + 3
    while time.time() < end and not (STATE["A"]["quit"] and STATE["B"]["quit"]):
        pump(0.1)
    checks["exit code 0"] = rc == 0
    checks["QUIT reached server one"] = STATE["A"]["quit"]
    checks["QUIT reached server two"] = STATE["B"]["quit"]

    # --- /disconnect on the last remaining connection -----------------
    # The client has to say why it exits, and it can only say it after the
    # loop: the buffer that would hold the message is freed with the server's
    # tabs. So this reads the pty output after the process is gone, never a
    # buffer (that distinction cost a wrong green once already).
    d_master, d_slave = pty.openpty()
    fcntl.ioctl(d_slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    dproc = subprocess.Popen([BIN, HOST, "tester", str(PORT_A)],
                             stdin=d_slave, stdout=d_slave, stderr=d_slave,
                             close_fds=True)
    os.close(d_slave)
    dout = bytearray()

    def drain(seconds):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([d_master], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(d_master, 65536)
                except OSError:
                    return
                if not chunk:
                    return
                dout.extend(chunk)

    drain(1.5)
    os.write(d_master, b"/disconnect\n")
    drain(1.0)
    try:
        drc = dproc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        dproc.kill()
        dproc.wait()
        drc = -1
    drain(0.5)
    dtext = strip_ansi(dout.decode(errors="replace"))
    checks["last /disconnect exits 0"] = drc == 0
    checks["last /disconnect says why it exits"] = "no server left" in dtext
    os.close(d_master)

    print("=== SERVER A LOG ===")
    for l in STATE["A"]["log"]:
        print("A>> " + l)
    print("=== SERVER B LOG ===")
    for l in STATE["B"]["log"]:
        print("B>> " + l)
    print("=== LAST FRAME ===")
    fs = frames()
    if fs:
        print(fs[-1][:4000])
    print("=== CHECKS ===")
    for k, v in checks.items():
        print(("OK   " if v else "FAIL ") + k)
    sys.exit(0 if all(checks.values()) else 1)


main()
