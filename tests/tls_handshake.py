#!/usr/bin/env python3
"""Offline TLS checks: no real IRC server, no outside network.

Spins a local TLS server (and a local plaintext one) on 127.0.0.1, runs
./syschat inside a pty and checks what actually goes over the wire:

  - --insecure + TLS server  -> the IRC traffic is encrypted, banner says TLS
  - --tls + plaintext server -> handshake fails, clear fallback, session works
  - port 6697, no flag       -> TLS is implied
  - any other port, no flag  -> still plaintext, historic behaviour

The certificate is self-signed and generated on the fly with the openssl CLI,
which is why the client is run with --insecure here.

Usage: ./build.sh && python3 tests/tls_handshake.py
"""
import os, pty, select, shutil, socket, ssl, subprocess, sys, tempfile, termios, threading, time, fcntl, struct

HOST = "127.0.0.1"
CASES = {}


def strip_ansi(a):
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
        r.append(a[k])
        k += 1
    return "".join(r)


def make_cert(tmp):
    cert, key = os.path.join(tmp, "cert.pem"), os.path.join(tmp, "key.pem")
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
         "-keyout", key, "-out", cert, "-days", "1", "-subj", "/CN=localhost"],
        check=True, capture_output=True)
    return cert, key


def tls_context(cert, key):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    return ctx


def server(port, tls, log, ready, drop_first=False, ctx=None):
    """One listener, up to two connections: `drop_first` closes the first one
    immediately, which is what a plaintext IRC server does to a ClientHello."""
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # A port used by the previous case can still be in TIME_WAIT; retry briefly
    # instead of reporting a TLS failure that is only a socket state.
    for attempt in range(10):
        try:
            srv.bind((HOST, port))
            break
        except OSError:
            if attempt == 9:
                ready.set()
                return
            time.sleep(0.2)
    srv.listen(2)
    ready.set()
    srv.settimeout(20)
    deadline = time.time() + 30
    seen = 0
    while time.time() < deadline and seen < 2:
        try:
            conn, _ = srv.accept()
        except (socket.timeout, OSError):
            break
        seen += 1
        if drop_first and seen == 1:
            try:
                conn.recv(4096)
            except OSError:
                pass
            conn.close()
            continue
        try:
            if tls:
                assert ctx is not None
                conn = ctx.wrap_socket(conn, server_side=True)
        except (ssl.SSLError, OSError) as exc:
            # A refused handshake (bad certificate) must not kill the listener:
            # the client's plaintext fallback reconnects on the same port, and
            # that second connection is exactly what this case checks.
            log.append("TLS-FAIL %r" % (exc,))
            conn.close()
            continue
        conn.settimeout(0.2)
        buf = b""
        while time.time() < deadline:
            try:
                data = conn.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not data:
                break
            buf += data
            while b"\r\n" in buf:
                line, buf = buf.split(b"\r\n", 1)
                line = line.decode(errors="replace")
                log.append("C>> " + line)
                if line.split(" ")[0].upper() == "USER":
                    nick = line.split(" ")[1] if " " in line else "tester"
                    conn.sendall((":mock 001 %s :Welcome over %s\r\n"
                                  % (nick, "TLS" if tls else "plain")).encode())
                if line.split(" ")[0].upper() == "NICK":
                    conn.sendall(b":mock NOTICE tester :tag-%s\r\n"
                                 % (b"tls" if tls else b"plain"))
        conn.close()
        if not drop_first:
            break
    srv.close()


def run_client(args, seconds=2.5, env=None):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    child_env = dict(os.environ)
    child_env.update(env or {})
    proc = subprocess.Popen(["./syschat"] + args, stdin=slave, stdout=slave,
                            stderr=slave, close_fds=True, env=child_env)
    os.close(slave)
    out = bytearray()
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            if not chunk:
                break
            out.extend(chunk)
    try:
        os.write(master, b"/exit\n")
    except OSError:
        pass
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    try:
        os.close(master)
    except OSError:
        pass
    return strip_ansi(out.decode(errors="replace"))


def case(name, port, tls, flags, cert, key, drop_first=False, wait=2.5, env=None):
    log, ready = [], threading.Event()
    ctx = tls_context(cert, key) if tls and not drop_first else None
    th = threading.Thread(target=server,
                          args=(port, tls and not drop_first, log, ready, drop_first, ctx),
                          daemon=True)
    th.start()
    if not ready.wait(10):
        # Another test (or a colleague's run in the same checkout) already owns
        # this port. Skipping is honest: a bind conflict is not a TLS failure,
        # and the same logic is covered by the other cases.
        print("SKIP %s (port %d busy)" % (name, port))
        return None, None
    text = run_client([HOST, "tester", str(port)] + flags, seconds=wait, env=env)
    th.join(timeout=5)
    return log, text


def main():
    if not shutil.which("openssl"):
        print("SKIP openssl CLI missing, cannot generate a test certificate")
        return 0
    tmp = tempfile.mkdtemp(prefix="syschat-tls-")
    cert, key = make_cert(tmp)

    log, text = case("tls encrypted session (--insecure)", 16967, True, ["--insecure"], cert, key)
    if text is not None:
        assert log is not None
        CASES["TLS: server received PASS/NICK/USER decrypted"] = any("PASS none" in l for l in log) \
            and any("NICK tester" in l for l in log) and any("USER tester" in l for l in log)
        CASES["TLS: banner announces TLSv1.3"] = "TLSv1.3" in text
        CASES["TLS: banner says cert NOT verified with --insecure"] = "NOT verified" in text
        CASES["TLS: server answer reaches the client"] = "Welcome over TLS" in text

    log, text = case("plaintext fallback (--tls on a plain server)", 16968, False,
                     ["--tls"], cert, key, drop_first=True, wait=3.5)
    if text is not None:
        assert log is not None
        # The buffer wraps long lines, so only the fragments that stay on one
        # physical row can be asserted on.
        CASES["fallback: handshake failure is announced in clear"] = "handshake failed" in text \
            and "NOT encrypted" in text
        CASES["fallback: session still works over plaintext"] = any(l.startswith("C>> NICK") for l in log)
        CASES["fallback: banner says plaintext"] = "plaintext" in text

    # No flag at all: the port alone must select TLS. The self-signed test
    # certificate is trusted-off through the env var, not through --insecure,
    # so this still proves the port 6697 default.
    log, text = case("port 6697 implies TLS", 6697, True, [], cert, key,
                     env={"SYSCHAT_TLS_NO_VERIFY": "1"})
    if text is not None:
        CASES["6697: TLS used without any flag"] = "TLSv1.3" in text and "plaintext" not in text

    # Security-critical: without --insecure a self-signed certificate must be
    # refused, and the refusal must be announced rather than silently ignored.
    log, text = case("self-signed refused without --insecure", 16969, True,
                     ["--tls"], cert, key, wait=3.0)
    if text is not None:
        CASES["verify: self-signed cert refused without --insecure"] = \
            "handshake failed" in text and "TLSv1.3" not in text

    # 16966, not 16667: that one belongs to functional_buffers.py, and two
    # suites running side by side in the same checkout would collide.
    log, text = case("port 6667-style plaintext default", 16966, False, [], cert, key)
    if text is not None:
        CASES["other port: no TLS by default"] = "plaintext" in text and "TLSv1.3" not in text

    shutil.rmtree(tmp, ignore_errors=True)
    for name, ok in CASES.items():
        print(("OK   " if ok else "FAIL ") + name)
    return 0 if all(CASES.values()) else 1


sys.exit(main())
