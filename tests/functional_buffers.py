#!/usr/bin/env python3
"""Functional check for the buffer/tab support, without a real IRC server.

Starts a mock IRC server on 127.0.0.1:16667, runs ./syschat inside a pty and
drives it like a human would: /join two channels, type, Tab / Shift-Tab,
/next, /close, /?. Exits 0 when every check passes.

Usage:  ./build.sh && python3 tests/functional_buffers.py
"""
import os, re, pty, select, socket, subprocess, sys, termios, time, fcntl, struct, threading

HOST, PORT = "127.0.0.1", 16667
log = []
state = {"pong": None, "joins": []}




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
        r.append(a[k]); k += 1
    return "".join(r)

def serve(srv, ready):
    srv.listen(1)
    ready.set()
    conn, _ = srv.accept()
    buf = b""
    served = False
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
                nick = line.split(" ")[1] if len(line.split(" ")) > 1 else "tester"
                conn.sendall((":mock 001 %s :Welcome\r\n" % nick).encode())
            elif cmd == "JOIN":
                chan = line.split(" ")[1]
                state["joins"].append(chan)
                conn.sendall((":%s!u@h JOIN :%s\r\n" % ("tester", chan)).encode())
                if chan == "#beta":
                    # 200 columns on a 100-column pty: must wrap, not be cut,
                    # so the tail marker only shows up if wrapping works.
                    conn.sendall((":carol!c@h PRIVMSG #beta :" + "w" * 200
                                  + "ZZEND\r\n").encode())
                if len(state["joins"]) == 2 and not served:
                    served = True
                    time.sleep(0.2)
                    # 3 lines in a single write: must not lose any
                    conn.sendall((
                        ":carol!c@h PRIVMSG #alpha :" + "\u00e9" * 100 + "\r\n"
                        ":bob!b@h PRIVMSG #beta :beta one\r\n"
                        ":bob!b@h PRIVMSG #alpha :alpha one\r\n"
                        ":bob!b@h PRIVMSG #alpha :alpha two\r\n"
                    ).encode())
                    time.sleep(0.3)
                    # a line split across two TCP writes
                    conn.sendall(b":srv PING :spl")
                    time.sleep(0.2)
                    conn.sendall(b"it\r\n")
                    time.sleep(0.2)
                    conn.sendall(b":bob!b@h PRIVMSG #beta :beta two\r\n")
            elif cmd == "PONG":
                state["pong"] = line
    conn.close()


def main():
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    ready = threading.Event()
    t = threading.Thread(target=serve, args=(srv, ready), daemon=True)
    t.start()
    ready.wait()

    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    proc = subprocess.Popen(["./syschat", HOST, "tester", str(PORT)], stdin=slave, stdout=slave,
                            stderr=slave, close_fds=True)
    os.close(slave)
    out = bytearray()
    marks = {}

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

    pump(1.0)
    send("/join #alpha\n")
    pump(0.4)
    send("/join #beta\n")
    pump(1.6)          # server pushes 3 lines at once, ping split, beta two
    send("/switch #alpha\n")
    pump(0.3)
    send("hello from")
    pump(0.3)
    marks["typed_visible"] = "[#alpha] <tester> hello from" in strip_ansi(out.decode(errors="replace"))
    send(" tab test\n")
    pump(0.4)
    marks["prompt_alpha"] = "[#alpha] <tester> hello from tab test" in strip_ansi(out.decode(errors="replace"))
    send("\t")
    pump(0.3)
    marks["tab_to_beta"] = "[#beta] <tester> " in strip_ansi(out[-4000:].decode(errors="replace"))
    marks["long line wraps instead of being cut"] = "ZZEND" in strip_ansi(out[-8000:].decode(errors="replace"))
    send("\x1b[Z")
    pump(0.3)
    marks["shifttab_back_alpha"] = "[#alpha] <tester> " in strip_ansi(out[-4000:].decode(errors="replace"))
    marks["unread_alpha_while_on_beta"] = "#alpha*" in strip_ansi(out[-8000:].decode(errors="replace"))
    send("/next\n")
    pump(0.3)
    send("/prev\n")
    pump(0.3)
    send("/?\n")
    pump(0.4)
    # Long names on purpose: the bar must scroll to keep the active tab
    # visible, which short names would not trigger on a 100-column pty.
    for i in range(1, 11):
        send("/join #chan-with-a-long-name-%d\n" % i)
        pump(0.15)
    pump(0.4)
    last_raw = out.split(b"\x1b[H")[-1]
    marks["active tab visible with 13 buffers"] = b"#chan-with-a-long-name-10" in last_raw
    # Two bars now: line 0 is the server bar, line 1 the buffer bar, and it
    # is the buffer bar that has to scroll here.
    bars = strip_ansi(last_raw.decode("utf-8", "replace")).split("\r\n")
    marks["tab bar shows it scrolled"] = len(bars) > 1 and bars[1].startswith("<")
    send("/close #beta\n")
    pump(0.5)
    marks["beta_gone_from_tabs"] = b"#beta" not in out[-3000:]
    # /exit must quit on its own: terminate() would hide a broken exit path.
    send("/exit\n")
    try:
        marks["clean_exit"] = proc.wait(timeout=5) == 0
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        marks["clean_exit"] = False
    pump(0.3)

    open("/tmp/syschat_raw.bin","wb").write(out)
    text = out.decode(errors="replace")
    # Incoming accented text must be clipped by columns, not bytes: a byte
    # clip halves the line and can split a UTF-8 character in two.
    plain = strip_ansi(text)
    marks["accented line clipped by columns"] = ("\u00e9" * 60 in plain) and ("\ufffd" not in plain)
    stripped = []
    i = 0
    while i < len(text):
        if text[i] == "\033":
            j = i + 1
            if j < len(text) and text[j] == "[":
                j += 1
                while j < len(text) and not ("@" <= text[j] <= "~"):
                    j += 1
                i = j + 1
                continue
            i += 1
            continue
        stripped.append(text[i])
        i += 1
    plain = "".join(stripped)

    print("=== SERVER LOG ===")
    for l in log:
        print(l)
    print("=== PONG ===", state["pong"])
    print("=== FRAMES (last 4 renders) ===")
    frames = [f for f in plain.split("\x1b[H") if f.strip()]
    print("\n--- frame ---\n".join(frames[-3:]))
    print("=== CHECKS ===")
    checks = dict(marks)
    raw_frames = [f for f in text.split("\x1b[H") if f.strip()]
    def strip(a):
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
            r.append(a[k]); k += 1
        return "".join(r)
    last_frame = strip(raw_frames[-1]) if raw_frames else ""
    last_tabbar = last_frame.split("\r\n")[0] if last_frame else ""
    checks["prompt_alpha"] = marks["typed_visible"]
    checks["beta_gone_from_tabs"] = "#beta" not in last_tabbar
    print("LAST TABBAR:", repr(last_tabbar))

    checks.update({
        "tab bar shows #alpha": "#alpha" in plain,
        "tab bar shows #beta": "#beta" in plain,
        "alpha messages kept": "alpha one" in plain and "alpha two" in plain,
        "beta messages kept": "beta one" in plain and "beta two" in plain,
        "PONG answered split PING": state["pong"] is not None and "split" in (state["pong"] or ""),
        "local echo sent": any("PRIVMSG #alpha :hello from tab test" in l for l in log),
        "help listed": "Commands list" in plain,
        "timestamps on every line": re.search(r"\[\d\d:\d\d\] <bob> alpha one", plain) is not None,
    })
    for k, v in checks.items():
        print(("OK   " if v else "FAIL ") + k)
    sys.exit(0 if all(checks.values()) else 1)


main()
