from __future__ import annotations

import os
import shlex
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "artifacts" / "real_screenshots"

SCENARIOS = [
    ("01_start_services", "start", "启动服务与生成证书"),
    ("02_dns", "dns", "DNS 服务器"),
    ("03_http", "http", "Web 服务器"),
    ("04_virtual_hosts", "vhost", "虚拟主机"),
    ("05_auth_rate", "auth", "访问控制与流量控制"),
    ("06_https_cert", "https", "安全站点与证书"),
    ("07_ftp", "ftp", "FTP 服务器"),
    ("08_smb", "smb", "SMB 共享"),
    ("09_ssh", "ssh", "SSH 服务器"),
    ("10_mail", "mail", "SMTP、POP3 与 IMAP"),
]


def osa(script: str) -> str:
    result = subprocess.run(
        ["osascript", "-e", script],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=True,
    )
    return result.stdout.strip()


def open_terminal(command: str) -> None:
    osa(
        'tell application "Terminal"\n'
        "activate\n"
        f"do script {command!r}\n"
        "delay 0.2\n"
        "set bounds of front window to {120, 120, 1180, 760}\n"
        'tell application "System Events" to set frontmost of process "Terminal" to true\n'
        "end tell"
    )


def close_front_terminal() -> None:
    subprocess.run(
        [
            "osascript",
            "-e",
            'tell application "Terminal" to close front window',
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def capture_region(path: Path) -> None:
    subprocess.run(
        ["screencapture", "-x", "-R", "120,120,1060,640", str(path)],
        check=True,
    )


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for key, scenario, title in SCENARIOS:
        marker = OUT_DIR / f".{key}.done"
        marker.unlink(missing_ok=True)
        output = OUT_DIR / f"{key}.png"
        command = (
            f"cd {shlex.quote(str(ROOT))}; "
            "clear; "
            f"printf '实验八 应用层协议服务配置 - {title}\\n\\n'; "
            f"LAB8_IMPACKET_PATH=/tmp/codex_lab8_py python3 scripts/live_check.py {scenario}; "
            f"touch {shlex.quote(str(marker))}; "
            "sleep 2"
        )
        open_terminal(command)
        deadline = time.time() + 45
        while time.time() < deadline and not marker.exists():
            time.sleep(0.5)
        if not marker.exists():
            raise TimeoutError(f"scenario timed out: {scenario}")
        time.sleep(0.4)
        osa('tell application "Terminal" to activate')
        capture_region(output)
        time.sleep(2.0)
        close_front_terminal()
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

