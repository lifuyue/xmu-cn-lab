from __future__ import annotations

import contextlib
import ftplib
import imaplib
import io
import os
import poplib
import shlex
import smtplib
import subprocess
import sys
from email.message import EmailMessage
from pathlib import Path

from lab8_services import (
    ARTIFACTS,
    CERT_DIR,
    DNS_PORT,
    FTP_PORT,
    HTTP_PORT,
    HTTPS_PORT,
    IMAP_PORT,
    LAB_PASSWORD,
    LAB_USER,
    POP3_PORT,
    SMTP_PORT,
    SSH_DIR,
    SSH_PORT,
    ServiceManager,
    generate_certificates,
)


ROOT = Path(__file__).resolve().parents[1]
KNOWN_HOSTS = SSH_DIR / "known_hosts"


def run(args: list[str], timeout: int = 15, env: dict[str, str] | None = None) -> None:
    print("$ " + shlex.join(args))
    result = subprocess.run(
        args,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        env=env,
    )
    print(result.stdout.rstrip())
    if result.returncode != 0:
        print(f"[exit status: {result.returncode}]")
    print()


def start_services() -> ServiceManager:
    generate_certificates()
    manager = ServiceManager()
    print("生成本地 CA、服务器证书和 SSH 密钥。")
    print(manager.start())
    print()
    return manager


def check_start() -> None:
    manager = start_services()
    print("服务启动验证完成，随后停止本次演示服务。")
    print(manager.stop())


def check_dns() -> None:
    manager = start_services()
    try:
        run(["dig", "@127.0.0.1", "-p", str(DNS_PORT), "www.lab8.local", "+short"])
        run(["nslookup", f"-port={DNS_PORT}", "private.lab8.local", "127.0.0.1"])
    finally:
        print(manager.stop())


def check_http() -> None:
    manager = start_services()
    try:
        run(["curl", "--noproxy", "*", "-sS", "-i", f"http://127.0.0.1:{HTTP_PORT}/"])
    finally:
        print(manager.stop())


def check_vhost() -> None:
    manager = start_services()
    try:
        for host in ("site-a.lab8.local", "site-b.lab8.local"):
            run(
                [
                    "curl",
                    "--noproxy",
                    "*",
                    "-sS",
                    "-H",
                    f"Host: {host}",
                    f"http://127.0.0.1:{HTTP_PORT}/",
                ]
            )
    finally:
        print(manager.stop())


def check_auth() -> None:
    manager = start_services()
    try:
        run(
            [
                "curl",
                "--noproxy",
                "*",
                "-sS",
                "-o",
                "/dev/null",
                "-w",
                "anonymous status=%{http_code}\\n",
                f"http://127.0.0.1:{HTTP_PORT}/private",
            ]
        )
        run(
            [
                "curl",
                "--noproxy",
                "*",
                "-sS",
                "-u",
                f"{LAB_USER}:{LAB_PASSWORD}",
                "-o",
                "/dev/null",
                "-w",
                "authenticated status=%{http_code}\\n",
                f"http://127.0.0.1:{HTTP_PORT}/private",
            ]
        )
        run(
            [
                "curl",
                "--noproxy",
                "*",
                "--limit-rate",
                "16K",
                "-o",
                str(ARTIFACTS / "rate_limited_download.txt"),
                "-w",
                "downloaded=%{size_download} speed=%{speed_download}\\n",
                f"http://127.0.0.1:{HTTP_PORT}/rate-limited",
            ],
            timeout=20,
        )
    finally:
        print(manager.stop())


def check_https() -> None:
    manager = start_services()
    try:
        run(
            [
                "openssl",
                "x509",
                "-in",
                str(CERT_DIR / "server.crt"),
                "-noout",
                "-issuer",
                "-subject",
                "-dates",
            ]
        )
        run(
            [
                "curl",
                "--noproxy",
                "*",
                "--cacert",
                str(CERT_DIR / "lab8_ca.crt"),
                "--resolve",
                f"private.lab8.local:{HTTPS_PORT}:127.0.0.1",
                "-sS",
                "-o",
                "/dev/null",
                "-w",
                "https status=%{http_code} ssl_verify=%{ssl_verify_result}\\n",
                f"https://private.lab8.local:{HTTPS_PORT}/",
            ]
        )
    finally:
        print(manager.stop())


def check_ftp() -> None:
    manager = start_services()
    try:
        print("$ python ftp client")
        upload_payload = b"Uploaded through the local FTP service.\n"
        with ftplib.FTP() as ftp:
            ftp.connect("127.0.0.1", FTP_PORT, timeout=5)
            print(ftp.getwelcome())
            ftp.login(LAB_USER, LAB_PASSWORD)
            print("login: successful")
            ftp.storbinary("STOR upload_from_client.txt", io.BytesIO(upload_payload))
            print("upload: upload_from_client.txt")
            print("list:")
            for item in ftp.nlst():
                print("  " + item)
            try:
                ftp.mkd("not_allowed")
            except ftplib.error_perm as exc:
                print("mkdir denied:", exc)
            retrieved = io.BytesIO()
            ftp.retrbinary("RETR upload_from_client.txt", retrieved.write)
            print("download:", retrieved.getvalue().decode("utf-8").strip())
            ftp.quit()
    finally:
        print()
        print(manager.stop())


def check_smb() -> None:
    env = os.environ.copy()
    env["LAB8_IMPACKET_PATH"] = env.get("LAB8_IMPACKET_PATH", "/tmp/codex_lab8_py")
    run([sys.executable, str(ROOT / "scripts" / "smb_check.py")], timeout=20, env=env)


def check_ssh() -> None:
    manager = start_services()
    try:
        user = os.environ.get("USER", "lifuyue")
        run(
            [
                "ssh",
                "-p",
                str(SSH_PORT),
                "-i",
                str(SSH_DIR / "lab8_client_key"),
                "-o",
                "IdentitiesOnly=yes",
                "-o",
                "StrictHostKeyChecking=no",
                "-o",
                f"UserKnownHostsFile={KNOWN_HOSTS}",
                "-o",
                "PasswordAuthentication=no",
                f"{user}@127.0.0.1",
                "echo SSH_OK && whoami && pwd",
            ],
            timeout=15,
        )
    finally:
        print(manager.stop())


def check_mail() -> None:
    manager = start_services()
    try:
        print("$ python smtp/pop3/imap client")
        message = EmailMessage()
        message["From"] = "student@mail.lab8.local"
        message["To"] = "receiver@mail.lab8.local"
        message["Subject"] = "chapter 8 local mail test"
        message.set_content("SMTP delivered this message; POP3 and IMAP both read it.")

        with smtplib.SMTP("127.0.0.1", SMTP_PORT, timeout=5) as smtp:
            code, banner = smtp.ehlo()
            print(f"SMTP EHLO: {code} {banner.decode('utf-8', 'replace')}")
            smtp.send_message(message)
            print("SMTP send: queued")

        pop = poplib.POP3("127.0.0.1", POP3_PORT, timeout=5)
        print(pop.getwelcome().decode("utf-8", "replace"))
        pop.user(LAB_USER)
        pop.pass_(LAB_PASSWORD)
        count, size = pop.stat()
        print(f"POP3 STAT: messages={count} bytes={size}")
        _, lines, _ = pop.retr(1)
        subject = next((line for line in lines if line.lower().startswith(b"subject:")), b"")
        print("POP3 RETR:", subject.decode("utf-8", "replace"))
        pop.quit()

        imap = imaplib.IMAP4("127.0.0.1", IMAP_PORT)
        print("IMAP login:", imap.login(LAB_USER, LAB_PASSWORD)[0])
        print("IMAP select:", imap.select("INBOX")[1][0].decode("ascii"))
        status, payload = imap.fetch("1", "(RFC822)")
        print("IMAP fetch:", status, f"{len(payload[0][1])} bytes")
        imap.logout()
    finally:
        print()
        print(manager.stop())


CHECKS = {
    "start": check_start,
    "dns": check_dns,
    "http": check_http,
    "vhost": check_vhost,
    "auth": check_auth,
    "https": check_https,
    "ftp": check_ftp,
    "smb": check_smb,
    "ssh": check_ssh,
    "mail": check_mail,
}


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in CHECKS:
        print("Usage: python3 scripts/live_check.py <" + "|".join(CHECKS) + ">")
        return 2
    with contextlib.suppress(KeyboardInterrupt):
        CHECKS[sys.argv[1]]()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

