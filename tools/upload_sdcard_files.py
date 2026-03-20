#!/usr/bin/env python3
"""Upload SD card payload files to the ESP32 web uploader endpoint.

This script pushes files from local `sdcard/` to the device endpoint:
    POST /api/sd/upload?path=/icons/... or /fonts/... or /ui/...

Example:
  python tools/upload_sdcard_files.py --host 192.168.1.42

Requirements:
- Firmware with ENABLE_UI_WEB_TUNER true
- Device reachable over HTTP on port 80 (or use --port)
"""

from __future__ import annotations

import argparse
import mimetypes
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Iterable, List, Tuple


def iter_payload_files(root: Path, subdirs: List[str]) -> Iterable[Tuple[Path, str]]:
    for subdir in subdirs:
        local_base = root / subdir
        if not local_base.exists():
            continue
        for file_path in sorted(local_base.rglob("*")):
            if not file_path.is_file():
                continue
            remote_path = "/" + file_path.relative_to(root).as_posix()
            yield file_path, remote_path


def build_multipart_body(field_name: str, file_name: str, file_bytes: bytes, content_type: str, boundary: str) -> bytes:
    parts = []
    parts.append(f"--{boundary}\r\n".encode("utf-8"))
    parts.append(
        (
            f'Content-Disposition: form-data; name="{field_name}"; '
            f'filename="{file_name}"\r\n'
        ).encode("utf-8")
    )
    parts.append(f"Content-Type: {content_type}\r\n\r\n".encode("utf-8"))
    parts.append(file_bytes)
    parts.append(b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode("utf-8"))
    return b"".join(parts)


def upload_one(host: str, port: int, timeout: float, local_file: Path, remote_path: str, dry_run: bool) -> Tuple[bool, str]:
    encoded_path = urllib.parse.quote(remote_path, safe="/")
    url = f"http://{host}:{port}/api/sd/upload?path={encoded_path}"

    if dry_run:
        return True, f"DRY RUN -> {remote_path}"

    file_bytes = local_file.read_bytes()
    mime, _ = mimetypes.guess_type(local_file.name)
    content_type = mime or "application/octet-stream"
    boundary = "----esp32sduploadboundary"
    body = build_multipart_body("file", local_file.name, file_bytes, content_type, boundary)

    req = urllib.request.Request(url, data=body, method="POST")
    req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    req.add_header("Content-Length", str(len(body)))

    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            text = resp.read().decode("utf-8", errors="replace").strip()
            return resp.status == 200, text or "OK"
    except urllib.error.HTTPError as err:
        detail = err.read().decode("utf-8", errors="replace").strip()
        return False, f"HTTP {err.code}: {detail}"
    except Exception as err:  # noqa: BLE001
        return False, str(err)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Upload local sdcard payload files to ESP32 over HTTP")
    parser.add_argument("--host", required=True, help="ESP32 IP or hostname")
    parser.add_argument("--port", type=int, default=80, help="ESP32 web port (default: 80)")
    parser.add_argument("--timeout", type=float, default=15.0, help="HTTP timeout in seconds")
    parser.add_argument("--root", default="sdcard", help="Local SD payload root directory")
    parser.add_argument(
        "--only",
        choices=["icons", "fonts", "ui"],
        action="append",
        help="Upload only selected payload type(s). Can be repeated.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print actions without sending files")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    root = Path(args.root).resolve()
    if not root.exists() or not root.is_dir():
        print(f"ERROR: root directory does not exist: {root}")
        return 2

    subdirs = args.only if args.only else ["icons", "fonts", "ui"]
    files = list(iter_payload_files(root, subdirs))
    if not files:
        print(f"No files found under {root} for: {', '.join(subdirs)}")
        return 1

    print(f"Target: http://{args.host}:{args.port}")
    print(f"Root:   {root}")
    print(f"Files:  {len(files)}")

    failures = 0
    for local_file, remote_path in files:
        ok, msg = upload_one(
            host=args.host,
            port=args.port,
            timeout=args.timeout,
            local_file=local_file,
            remote_path=remote_path,
            dry_run=args.dry_run,
        )
        status = "OK" if ok else "FAIL"
        print(f"[{status}] {remote_path} <- {local_file} :: {msg}")
        if not ok:
            failures += 1

    if failures:
        print(f"\nCompleted with {failures} failure(s).")
        return 1

    print("\nCompleted successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
