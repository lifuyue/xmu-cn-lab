from __future__ import annotations

import subprocess
import sys
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOG_DIR = ROOT / "artifacts" / "logs"

SCENARIOS = ["start", "dns", "http", "vhost", "auth", "https", "ftp", "smb", "ssh", "mail"]


def main() -> int:
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    for scenario in SCENARIOS:
        print(f"== {scenario} ==")
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "live_check.py"), scenario],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env={**os.environ, "LAB8_IMPACKET_PATH": "/tmp/codex_lab8_py"},
        )
        (LOG_DIR / f"live_{scenario}.txt").write_text(result.stdout, encoding="utf-8")
        print(result.stdout)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
