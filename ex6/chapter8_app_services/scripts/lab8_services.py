from __future__ import annotations

import base64
import email.utils
import html
import os
import shlex
import shutil
import socket
import socketserver
import ssl
import struct
import subprocess
import threading
import time
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "artifacts"
LOG_DIR = ARTIFACTS / "logs"
CERT_DIR = ARTIFACTS / "certs"
FTP_ROOT = ARTIFACTS / "ftp_root"
SSH_DIR = ARTIFACTS / "ssh"
MAIL_DIR = ARTIFACTS / "mailbox"

DNS_PORT = 15353
HTTP_PORT = 8080
HTTPS_PORT = 8443
FTP_PORT = 2121
SSH_PORT = 2222
SMTP_PORT = 2525
POP3_PORT = 8110
IMAP_PORT = 8143

LAB_USER = "lab8"
LAB_PASSWORD = "network"

ZONE = {
    "www.lab8.local": "127.0.0.1",
    "site-a.lab8.local": "127.0.0.1",
    "site-b.lab8.local": "127.0.0.1",
    "private.lab8.local": "127.0.0.1",
    "ftp.lab8.local": "127.0.0.1",
    "ssh.lab8.local": "127.0.0.1",
    "mail.lab8.local": "127.0.0.1",
}


def ensure_layout() -> None:
    for path in (ARTIFACTS, LOG_DIR, CERT_DIR, FTP_ROOT, SSH_DIR, MAIL_DIR):
        path.mkdir(parents=True, exist_ok=True)
    (FTP_ROOT / "welcome.txt").write_text(
        "Welcome to the chapter 8 FTP service.\n", encoding="utf-8"
    )


def _run(args: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=True,
    )
    return "$ " + shlex.join(args) + "\n" + result.stdout


def generate_certificates() -> str:
    ensure_layout()
    openssl = shutil.which("openssl") or "/opt/homebrew/bin/openssl"
    ca_key = CERT_DIR / "lab8_ca.key"
    ca_crt = CERT_DIR / "lab8_ca.crt"
    server_key = CERT_DIR / "server.key"
    server_csr = CERT_DIR / "server.csr"
    server_crt = CERT_DIR / "server.crt"
    config = CERT_DIR / "server_openssl.cnf"

    config.write_text(
        """[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
CN = private.lab8.local
O = XMU Computer Networks Lab

[v3_req]
keyUsage = keyEncipherment, dataEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[alt_names]
DNS.1 = private.lab8.local
DNS.2 = www.lab8.local
DNS.3 = site-a.lab8.local
DNS.4 = site-b.lab8.local
IP.1 = 127.0.0.1
""",
        encoding="utf-8",
    )

    log: list[str] = []
    if not ca_key.exists():
        log.append(_run([openssl, "genrsa", "-out", str(ca_key), "2048"]))
    if not ca_crt.exists():
        log.append(
            _run(
                [
                    openssl,
                    "req",
                    "-x509",
                    "-new",
                    "-nodes",
                    "-key",
                    str(ca_key),
                    "-sha256",
                    "-days",
                    "365",
                    "-subj",
                    "/CN=Lab8 Local CA/O=XMU Computer Networks Lab",
                    "-out",
                    str(ca_crt),
                ]
            )
        )
    if not server_key.exists():
        log.append(_run([openssl, "genrsa", "-out", str(server_key), "2048"]))
    log.append(
        _run(
            [
                openssl,
                "req",
                "-new",
                "-key",
                str(server_key),
                "-out",
                str(server_csr),
                "-config",
                str(config),
            ]
        )
    )
    log.append(
        _run(
            [
                openssl,
                "x509",
                "-req",
                "-in",
                str(server_csr),
                "-CA",
                str(ca_crt),
                "-CAkey",
                str(ca_key),
                "-CAcreateserial",
                "-out",
                str(server_crt),
                "-days",
                "365",
                "-sha256",
                "-extensions",
                "v3_req",
                "-extfile",
                str(config),
            ]
        )
    )
    return "\n".join(log)


class ReuseThreadingTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


class ReuseThreadingUDPServer(socketserver.ThreadingMixIn, socketserver.UDPServer):
    allow_reuse_address = True
    daemon_threads = True


def _append_log(path: Path, line: str) -> None:
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    with path.open("a", encoding="utf-8") as f:
        f.write(f"[{timestamp}] {line}\n")


class DNSHandler(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        data, sock = self.request
        if len(data) < 12:
            return

        try:
            offset = 12
            labels: list[str] = []
            while offset < len(data):
                length = data[offset]
                offset += 1
                if length == 0:
                    break
                labels.append(data[offset : offset + length].decode("ascii", "ignore"))
                offset += length
            if offset + 4 > len(data):
                return
            qtype, qclass = struct.unpack("!HH", data[offset : offset + 4])
            qname = ".".join(labels).lower()
            answer_ip = ZONE.get(qname)
            has_answer = answer_ip is not None and qclass == 1 and qtype in (1, 255)
            header = (
                data[:2]
                + b"\x81\x80"
                + data[4:6]
                + struct.pack("!HHH", 1 if has_answer else 0, 0, 0)
            )
            question = data[12 : offset + 4]
            answer = b""
            if has_answer:
                answer = (
                    b"\xc0\x0c"
                    + struct.pack("!HHIH", 1, 1, 60, 4)
                    + socket.inet_aton(answer_ip)
                )
            sock.sendto(header + question + answer, self.client_address)
            _append_log(self.server.log_path, f"DNS {qname} -> {answer_ip or 'NXDOMAIN'}")
        except Exception as exc:  # pragma: no cover - diagnostic path
            _append_log(self.server.log_path, f"DNS error: {exc}")


class LabHTTPHandler(socketserver.StreamRequestHandler):
    server_version = "Lab8HTTP/1.0"

    def handle(self) -> None:
        request_line = self.rfile.readline(65537).decode("iso-8859-1").strip()
        if not request_line:
            return
        parts = request_line.split()
        if len(parts) < 2:
            self._send(400, "Bad Request", "text/plain", b"Bad request")
            return
        method, path = parts[0], parts[1]
        headers: dict[str, str] = {}
        while True:
            line = self.rfile.readline(65537).decode("iso-8859-1")
            if line in ("\r\n", "\n", ""):
                break
            key, _, value = line.partition(":")
            headers[key.lower()] = value.strip()

        host = headers.get("host", "127.0.0.1").split(":")[0].lower()
        _append_log(self.server.log_path, f"HTTP {method} host={host} path={path}")
        if method != "GET":
            self._send(405, "Method Not Allowed", "text/plain", b"Only GET is supported")
            return
        if path.startswith("/private"):
            expected = "Basic " + base64.b64encode(
                f"{LAB_USER}:{LAB_PASSWORD}".encode("ascii")
            ).decode("ascii")
            if headers.get("authorization") != expected:
                self._send(
                    401,
                    "Unauthorized",
                    "text/plain",
                    b"Authentication required",
                    extra_headers=[('WWW-Authenticate', 'Basic realm="lab8"')],
                )
                return
            body = _page(
                "Password protected area",
                "HTTP basic authentication succeeded for user lab8.",
            )
            self._send(200, "OK", "text/html; charset=utf-8", body)
            return
        if path.startswith("/rate-limited"):
            payload = (b"rate-limited-data\n" * 4096)
            self._send_headers(
                200,
                "OK",
                "text/plain",
                len(payload),
                extra_headers=[("X-Lab-Rate-Limit", "32KiB/s demo endpoint")],
            )
            for pos in range(0, len(payload), 8192):
                self.wfile.write(payload[pos : pos + 8192])
                self.wfile.flush()
                time.sleep(0.03)
            return

        title, message = _host_content(host, getattr(self.server, "is_https", False))
        self._send(200, "OK", "text/html; charset=utf-8", _page(title, message))

    def _send(
        self,
        status: int,
        reason: str,
        content_type: str,
        body: bytes,
        extra_headers: list[tuple[str, str]] | None = None,
    ) -> None:
        self._send_headers(status, reason, content_type, len(body), extra_headers)
        self.wfile.write(body)

    def _send_headers(
        self,
        status: int,
        reason: str,
        content_type: str,
        length: int,
        extra_headers: list[tuple[str, str]] | None = None,
    ) -> None:
        lines = [
            f"HTTP/1.1 {status} {reason}",
            f"Server: {self.server_version}",
            f"Date: {email.utils.formatdate(usegmt=True)}",
            f"Content-Type: {content_type}",
            f"Content-Length: {length}",
            "Connection: close",
        ]
        for key, value in extra_headers or []:
            lines.append(f"{key}: {value}")
        self.wfile.write(("\r\n".join(lines) + "\r\n\r\n").encode("iso-8859-1"))


def _host_content(host: str, is_https: bool) -> tuple[str, str]:
    if host == "site-a.lab8.local":
        return "Virtual host A", "Same IP and port, Host header selects site A."
    if host == "site-b.lab8.local":
        return "Virtual host B", "Same IP and port, Host header selects site B."
    if host == "private.lab8.local" or is_https:
        return "HTTPS private site", "TLS is enabled with a local CA-signed certificate."
    return "Chapter 8 service lab", "HTTP service is running on the local lab server."


def _page(title: str, message: str) -> bytes:
    return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>{html.escape(title)}</title>
  <style>
    body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; margin: 40px; }}
    main {{ max-width: 720px; }}
    code {{ background: #eef2f7; padding: 2px 5px; border-radius: 4px; }}
  </style>
</head>
<body>
  <main>
    <h1>{html.escape(title)}</h1>
    <p>{html.escape(message)}</p>
    <p>Service ports: DNS 15353, HTTP 8080, HTTPS 8443, FTP 2121, SSH 2222,
    SMTP 2525, POP3 8110, IMAP 8143.</p>
  </main>
</body>
</html>
""".encode("utf-8")


class FTPHandler(socketserver.StreamRequestHandler):
    def setup(self) -> None:
        super().setup()
        self.logged_in = False
        self.passive_socket: socket.socket | None = None

    def finish(self) -> None:
        if self.passive_socket:
            self.passive_socket.close()
        super().finish()

    def handle(self) -> None:
        self._reply("220 Lab8 FTP service ready")
        while True:
            line = self.rfile.readline(4096)
            if not line:
                break
            text = line.decode("utf-8", "ignore").strip()
            if not text:
                continue
            command, _, argument = text.partition(" ")
            command = command.upper()
            _append_log(self.server.log_path, f"FTP {command} {argument}".strip())
            if command == "USER":
                self._reply("331 User name okay, need password")
            elif command == "PASS":
                if argument == LAB_PASSWORD:
                    self.logged_in = True
                    self._reply("230 Login successful")
                else:
                    self._reply("530 Login incorrect")
            elif not self.logged_in:
                self._reply("530 Please login with USER and PASS")
            elif command == "SYST":
                self._reply("215 UNIX Type: L8")
            elif command == "PWD":
                self._reply('257 "/"')
            elif command == "TYPE":
                self._reply("200 Type set")
            elif command == "FEAT":
                self._reply("211 No features")
            elif command == "PASV":
                self._enter_passive()
            elif command in {"LIST", "NLST"}:
                self._list_files()
            elif command == "STOR":
                self._store_file(argument)
            elif command == "RETR":
                self._retrieve_file(argument)
            elif command in {"MKD", "RMD", "DELE"}:
                self._reply("550 Permission denied by lab policy")
            elif command == "QUIT":
                self._reply("221 Goodbye")
                break
            else:
                self._reply("502 Command not implemented")

    def _reply(self, line: str) -> None:
        self.wfile.write((line + "\r\n").encode("utf-8"))
        self.wfile.flush()

    def _enter_passive(self) -> None:
        if self.passive_socket:
            self.passive_socket.close()
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("127.0.0.1", 0))
        sock.listen(1)
        self.passive_socket = sock
        port = sock.getsockname()[1]
        self._reply(f"227 Entering Passive Mode (127,0,0,1,{port // 256},{port % 256})")

    def _accept_data(self) -> socket.socket:
        if not self.passive_socket:
            raise RuntimeError("PASV must be called before data transfer")
        conn, _ = self.passive_socket.accept()
        self.passive_socket.close()
        self.passive_socket = None
        return conn

    def _list_files(self) -> None:
        self._reply("150 Opening data connection for file list")
        with self._accept_data() as conn:
            rows = []
            for file in sorted(FTP_ROOT.iterdir()):
                if file.is_file():
                    rows.append(f"-rw-r--r-- 1 lab8 lab8 {file.stat().st_size:8d} Jan 01 00:00 {file.name}")
            conn.sendall(("\r\n".join(rows) + "\r\n").encode("utf-8"))
        self._reply("226 Transfer complete")

    def _store_file(self, name: str) -> None:
        target = FTP_ROOT / Path(name).name
        self._reply("150 Opening data connection for upload")
        with self._accept_data() as conn, target.open("wb") as f:
            while True:
                chunk = conn.recv(65536)
                if not chunk:
                    break
                f.write(chunk)
        self._reply("226 Upload complete")

    def _retrieve_file(self, name: str) -> None:
        target = FTP_ROOT / Path(name).name
        if not target.exists():
            self._reply("550 File not found")
            return
        self._reply("150 Opening data connection for download")
        with self._accept_data() as conn, target.open("rb") as f:
            while True:
                chunk = f.read(65536)
                if not chunk:
                    break
                conn.sendall(chunk)
        self._reply("226 Download complete")


class SMTPHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        self._send("220 mail.lab8.local ESMTP ready")
        sender = ""
        recipients: list[str] = []
        while True:
            line = self.rfile.readline(65536).decode("utf-8", "replace").rstrip("\r\n")
            if not line:
                break
            command = line.split(" ", 1)[0].upper()
            _append_log(self.server.log_path, f"SMTP {line}")
            if command in {"EHLO", "HELO"}:
                self._send("250-mail.lab8.local")
                self._send("250 HELP")
            elif command == "MAIL":
                sender = line
                self._send("250 Sender accepted")
            elif command == "RCPT":
                recipients.append(line)
                self._send("250 Recipient accepted")
            elif command == "DATA":
                self._send("354 End data with <CR><LF>.<CR><LF>")
                lines = []
                while True:
                    data_line = self.rfile.readline(65536)
                    if data_line in (b".\r\n", b".\n", b"."):
                        break
                    lines.append(data_line)
                raw = b"".join(lines)
                self.server.mailbox.append(
                    {"sender": sender, "recipients": recipients[:], "raw": raw}
                )
                (MAIL_DIR / "last_message.eml").write_bytes(raw)
                self._send("250 Message queued")
            elif command == "RSET":
                sender = ""
                recipients = []
                self._send("250 Reset")
            elif command == "QUIT":
                self._send("221 Bye")
                break
            else:
                self._send("250 OK")

    def _send(self, line: str) -> None:
        self.wfile.write((line + "\r\n").encode("utf-8"))
        self.wfile.flush()


class POP3Handler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        deleted: set[int] = set()
        self._send("+OK lab8 POP3 ready")
        while True:
            line = self.rfile.readline(4096).decode("utf-8", "replace").strip()
            if not line:
                break
            command, _, argument = line.partition(" ")
            command = command.upper()
            _append_log(self.server.log_path, f"POP3 {line}")
            messages = self.server.mailbox
            if command == "USER":
                self._send("+OK")
            elif command == "PASS":
                self._send("+OK logged in")
            elif command == "STAT":
                active = [m for i, m in enumerate(messages, 1) if i not in deleted]
                size = sum(len(m["raw"]) for m in active)
                self._send(f"+OK {len(active)} {size}")
            elif command == "LIST":
                self._send(f"+OK {len(messages)} messages")
                for i, m in enumerate(messages, 1):
                    if i not in deleted:
                        self._send(f"{i} {len(m['raw'])}")
                self._send(".")
            elif command == "RETR":
                index = int(argument)
                raw = messages[index - 1]["raw"]
                self._send(f"+OK {len(raw)} octets")
                self.wfile.write(raw.replace(b"\n.", b"\n..") + b"\r\n.\r\n")
                self.wfile.flush()
            elif command == "DELE":
                deleted.add(int(argument))
                self._send("+OK deleted")
            elif command == "QUIT":
                if deleted:
                    self.server.mailbox[:] = [
                        m for i, m in enumerate(messages, 1) if i not in deleted
                    ]
                self._send("+OK bye")
                break
            else:
                self._send("-ERR unsupported")

    def _send(self, line: str) -> None:
        self.wfile.write((line + "\r\n").encode("utf-8"))
        self.wfile.flush()


class IMAPHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        self._send("* OK lab8 IMAP4rev1 ready")
        while True:
            line = self.rfile.readline(4096).decode("utf-8", "replace").strip()
            if not line:
                break
            parts = line.split(" ", 2)
            tag = parts[0]
            command = parts[1].upper() if len(parts) > 1 else ""
            _append_log(self.server.log_path, f"IMAP {line}")
            if command == "CAPABILITY":
                self._send("* CAPABILITY IMAP4rev1")
                self._send(f"{tag} OK CAPABILITY completed")
            elif command == "LOGIN":
                self._send(f"{tag} OK LOGIN completed")
            elif command == "SELECT":
                self._send(f"* {len(self.server.mailbox)} EXISTS")
                self._send("* FLAGS (\\Seen)")
                self._send(f"{tag} OK [READ-WRITE] SELECT completed")
            elif command == "FETCH":
                index = int(parts[2].split()[0]) if len(parts) > 2 else 1
                raw = self.server.mailbox[index - 1]["raw"]
                self.wfile.write(f"* {index} FETCH (RFC822 {{{len(raw)}}}\r\n".encode("utf-8"))
                self.wfile.write(raw + b")\r\n")
                self._send(f"{tag} OK FETCH completed")
            elif command == "LOGOUT":
                self._send("* BYE logout")
                self._send(f"{tag} OK LOGOUT completed")
                break
            else:
                self._send(f"{tag} OK {command} completed")

    def _send(self, line: str) -> None:
        self.wfile.write((line + "\r\n").encode("utf-8"))
        self.wfile.flush()


class ServiceManager:
    def __init__(self) -> None:
        self.servers: list[socketserver.BaseServer] = []
        self.threads: list[threading.Thread] = []
        self.processes: list[subprocess.Popen[str]] = []
        self.mailbox: list[dict[str, object]] = []

    def start(self) -> str:
        ensure_layout()
        log: list[str] = []
        log.append(self._start_dns())
        log.append(self._start_http())
        log.append(self._start_https())
        log.append(self._start_ftp())
        log.append(self._start_mail())
        log.append(self._start_sshd())
        return "\n".join(log)

    def stop(self) -> str:
        lines: list[str] = []
        for proc in self.processes:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3)
                lines.append(f"Stopped process pid={proc.pid}")
        for server in self.servers:
            server.shutdown()
            server.server_close()
            lines.append(f"Stopped {server.server_address}")
        return "\n".join(lines)

    def _serve(self, server: socketserver.BaseServer, name: str) -> str:
        thread = threading.Thread(target=server.serve_forever, name=name, daemon=True)
        thread.start()
        self.servers.append(server)
        self.threads.append(thread)
        return f"Started {name} on {server.server_address}"

    def _start_dns(self) -> str:
        server = ReuseThreadingUDPServer(("127.0.0.1", DNS_PORT), DNSHandler)
        server.log_path = LOG_DIR / "dns.log"
        return self._serve(server, "dns")

    def _start_http(self) -> str:
        server = ReuseThreadingTCPServer(("127.0.0.1", HTTP_PORT), LabHTTPHandler)
        server.log_path = LOG_DIR / "http.log"
        server.is_https = False
        return self._serve(server, "http")

    def _start_https(self) -> str:
        server = ReuseThreadingTCPServer(("127.0.0.1", HTTPS_PORT), LabHTTPHandler)
        server.log_path = LOG_DIR / "https.log"
        server.is_https = True
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(CERT_DIR / "server.crt"), str(CERT_DIR / "server.key"))
        server.socket = context.wrap_socket(server.socket, server_side=True)
        return self._serve(server, "https")

    def _start_ftp(self) -> str:
        server = ReuseThreadingTCPServer(("127.0.0.1", FTP_PORT), FTPHandler)
        server.log_path = LOG_DIR / "ftp.log"
        return self._serve(server, "ftp")

    def _start_mail(self) -> str:
        smtp = ReuseThreadingTCPServer(("127.0.0.1", SMTP_PORT), SMTPHandler)
        pop3 = ReuseThreadingTCPServer(("127.0.0.1", POP3_PORT), POP3Handler)
        imap = ReuseThreadingTCPServer(("127.0.0.1", IMAP_PORT), IMAPHandler)
        for server, log_name in (
            (smtp, "smtp.log"),
            (pop3, "pop3.log"),
            (imap, "imap.log"),
        ):
            server.mailbox = self.mailbox
            server.log_path = LOG_DIR / log_name
        return "\n".join(
            [
                self._serve(smtp, "smtp"),
                self._serve(pop3, "pop3"),
                self._serve(imap, "imap"),
            ]
        )

    def _start_sshd(self) -> str:
        prepare_ssh_material()
        sshd = Path("/usr/sbin/sshd")
        config = SSH_DIR / "sshd_config"
        log_path = LOG_DIR / "sshd.log"
        proc = subprocess.Popen(
            [str(sshd), "-D", "-e", "-f", str(config)],
            text=True,
            stdout=log_path.open("w", encoding="utf-8"),
            stderr=subprocess.STDOUT,
        )
        self.processes.append(proc)
        time.sleep(0.7)
        if proc.poll() is not None:
            detail = log_path.read_text(encoding="utf-8", errors="replace")
            raise RuntimeError(f"sshd failed to start:\n{detail}")
        return f"Started sshd on 127.0.0.1:{SSH_PORT} pid={proc.pid}"


def prepare_ssh_material() -> None:
    ensure_layout()
    ssh_keygen = shutil.which("ssh-keygen") or "/usr/bin/ssh-keygen"
    host_key = SSH_DIR / "ssh_host_ed25519_key"
    client_key = SSH_DIR / "lab8_client_key"
    if not host_key.exists():
        _run([ssh_keygen, "-t", "ed25519", "-N", "", "-f", str(host_key)])
    if not client_key.exists():
        _run([ssh_keygen, "-t", "ed25519", "-N", "", "-f", str(client_key)])
    os.chmod(host_key, 0o600)
    os.chmod(client_key, 0o600)
    authorized_keys = SSH_DIR / "authorized_keys"
    authorized_keys.write_text((client_key.with_suffix(".pub")).read_text(encoding="utf-8"))
    os.chmod(authorized_keys, 0o600)

    user = os.environ.get("USER") or os.getlogin()
    config = SSH_DIR / "sshd_config"
    config.write_text(
        f"""Port {SSH_PORT}
ListenAddress 127.0.0.1
HostKey {host_key}
PidFile {SSH_DIR / 'sshd.pid'}
AuthorizedKeysFile {authorized_keys}
PasswordAuthentication no
KbdInteractiveAuthentication no
PubkeyAuthentication yes
PermitRootLogin no
AllowUsers {user}
StrictModes no
UsePAM no
Subsystem sftp internal-sftp
LogLevel VERBOSE
""",
        encoding="utf-8",
    )


if __name__ == "__main__":
    generate_certificates()
    manager = ServiceManager()
    print(manager.start())
    print("Press Ctrl+C to stop services.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print(manager.stop())
