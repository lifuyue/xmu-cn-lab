from __future__ import annotations

import os
import sys
import threading
import time
from pathlib import Path


extra_path = os.environ.get("LAB8_IMPACKET_PATH", "/tmp/codex_lab8_py")
if Path(extra_path).exists():
    sys.path.insert(0, extra_path)

from impacket import smbserver
from impacket.smbconnection import SMBConnection


ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "artifacts"
SHARE_ROOT = ARTIFACTS / "smb_share"
LOG_DIR = ARTIFACTS / "logs"
PORT = 1445


def main() -> None:
    SHARE_ROOT.mkdir(parents=True, exist_ok=True)
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    (SHARE_ROOT / "smb_welcome.txt").write_text(
        "Welcome to the chapter 8 SMB service.\n", encoding="utf-8"
    )

    server = smbserver.SimpleSMBServer(listenAddress="127.0.0.1", listenPort=PORT)
    server.addShare("LAB8", str(SHARE_ROOT), "Lab8 SMB share")
    server.setSMB2Support(True)
    server.setLogFile(str(LOG_DIR / "smbserver.log"))
    thread = threading.Thread(target=server.start, daemon=True)
    thread.start()
    time.sleep(1)

    print(f"SMB server: 127.0.0.1:{PORT}", flush=True)
    print(f"Share root: {SHARE_ROOT}", flush=True)
    conn = SMBConnection("127.0.0.1", "127.0.0.1", sess_port=PORT, timeout=5)
    conn.login("", "")
    shares = [share["shi1_netname"][:-1] for share in conn.listShares()]
    print("shares:", ", ".join(shares), flush=True)
    names = [entry.get_longname() for entry in conn.listPath("LAB8", "*")]
    print("LAB8 entries:", ", ".join(names), flush=True)
    conn.logoff()

    # SimpleSMBServer.stop can block on macOS after the verification client
    # disconnects. The process is dedicated to this check, so exit directly.
    os._exit(0)


if __name__ == "__main__":
    main()

