# Chapter 8 application service configuration

This directory contains a local, reproducible implementation of the services
requested by chapter 8 of the computer networks lab manual.

The services run on loopback user-space ports so the lab can be completed
without changing system services:

- DNS: `127.0.0.1:15353`
- HTTP: `127.0.0.1:8080`
- HTTPS: `127.0.0.1:8443`
- FTP: `127.0.0.1:2121`
- SSH: `127.0.0.1:2222`
- SMTP: `127.0.0.1:2525`
- POP3: `127.0.0.1:8110`
- IMAP: `127.0.0.1:8143`
- SMB: `127.0.0.1:1445` (used by the optional Impacket SMB check)

Run the full verification workflow:

```bash
python3 scripts/run_checks.py
```

Capture real macOS Terminal screenshots for the report:

```bash
python3 scripts/capture_real_screenshots.py
python3 scripts/build_report.py
```

Generated evidence is written under `artifacts/`:

- `artifacts/logs/`: raw command and protocol verification logs
- `artifacts/real_screenshots/`: real `screencapture` PNG evidence used in the report
- `artifacts/real_screenshots_for_doc/`: compressed copies of real screenshots embedded in the DOCX
- `artifacts/certs/`: local CA and server certificate material
- `artifacts/ftp_root/`: FTP server root directory
- `artifacts/ssh/`: local host/client keys for the loopback SSH service
- `artifacts/smb_share/`: SMB share root used during verification

The SMB check uses Impacket. If it is not already available, install it outside
the repository and point the check to that path:

```bash
python3 -m pip install --target /tmp/codex_lab8_py impacket
LAB8_IMPACKET_PATH=/tmp/codex_lab8_py python3 scripts/run_checks.py
```
