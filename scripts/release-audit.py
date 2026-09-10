"""Redacted credential and package checks. Never prints matched credential values."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = [
    ("Google client secret", re.compile(rb"GOCSPX-[A-Za-z0-9_-]{10,}")),
    ("Google access token", re.compile(rb"ya29\.[A-Za-z0-9_-]{20,}")),
    ("Google refresh token", re.compile(rb"1//[A-Za-z0-9_-]{30,}")),
    ("GitHub token", re.compile(rb"(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})")),
    # TLS libraries contain PEM delimiter constants; a delimiter alone is not
    # a private key. Require an actual base64 body and matching end marker.
    ("private key", re.compile(
        rb"-----BEGIN ((?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY)-----"
        rb"[\r\n]+[A-Za-z0-9+/=\r\n]{64,}-----END \1-----")),
]
# Exact synthetic test values only. Do not allowlist entire files or folders.
SYNTHETIC = {b"GOCSPX-test-secret"}
PRIVATE_VALUES = []
FORBIDDEN = re.compile(
    r"(^|/)(?:\.deps|artifacts|build[^/]*|__pycache__)/|"
    r"(^|/)(?:client_secret[^/]*\.json|google-client[^/]*\.json|credentials[^/]*\.json|\.env(?:\..*)?)$|"
    r"\.(?:dpapi|sqlite3?|db)$", re.I)


def findings(data):
    # Scan ASCII and UTF-16LE string constants in binary releases.
    for view in (data, data.replace(b"\0", b"")):
        for label, value in PRIVATE_VALUES:
            if value in view:
                yield label
        for label, pattern in PATTERNS:
            for match in pattern.finditer(view):
                if match.group() not in SYNTHETIC:
                    yield label


def audit_worktree():
    names = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT).decode().split("\0")
    failures = []
    for name in filter(None, names):
        if FORBIDDEN.search(name):
            failures.append((name, "private/generated path"))
        path = ROOT / name
        if path.is_file():
            failures.extend((name, label) for label in set(findings(path.read_bytes())))
    return failures


def audit_history():
    lines = subprocess.check_output(
        ["git", "rev-list", "--objects", "--all"], cwd=ROOT).decode().splitlines()
    failures = []
    process = subprocess.Popen(["git", "cat-file", "--batch"], cwd=ROOT,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for line in lines:
            oid, _, name = line.partition(" ")
            process.stdin.write((oid + "\n").encode())
            process.stdin.flush()
            header = process.stdout.readline().split()
            if len(header) != 3:
                raise RuntimeError("Cannot inspect a Git object")
            size = int(header[2])
            data = process.stdout.read(size)
            if len(data) != size or process.stdout.read(1) != b"\n":
                raise RuntimeError("Incomplete Git object")
            if header[1] != b"blob":
                continue
            location = f"{oid[:12]}:{name}"
            if FORBIDDEN.search(name):
                failures.append((location, "private/generated historical path"))
            failures.extend((location, label) for label in set(findings(data)))
    finally:
        process.stdin.close()
        process.stdout.close()
        process.wait()
    return failures


def audit_package(path):
    failures = []
    required = {
        "broadcast-scheduler/bin/64bit/broadcast-scheduler.dll",
        "broadcast-scheduler/bin/64bit/Qt6HttpServer.dll",
        "broadcast-scheduler/bin/64bit/Qt6WebSockets.dll",
        "broadcast-scheduler/data/qt/tls/qschannelbackend.dll",
        "broadcast-scheduler/data/locale/en-US.ini",
        "broadcast-scheduler/data/locale/es-ES.ini",
        "broadcast-scheduler/data/messages.json",
        "broadcast-scheduler/data/LICENSE",
        "broadcast-scheduler/data/licenses/GPL-3.0.txt",
    }
    with zipfile.ZipFile(path) as package:
        names = set()
        for item in package.infolist():
            name = item.filename.replace("\\", "/")
            names.add(name)
            if (not name.startswith("broadcast-scheduler/") or ".." in name.split("/")
                    or FORBIDDEN.search(name)):
                failures.append((name, "unexpected package path"))
            if name.lower().endswith(".dll") and name not in required:
                failures.append((name, "unexpected dependency DLL"))
            if not item.is_dir():
                failures.extend((name, label) for label in set(findings(package.read(item))))
        failures.extend((name, "missing package file") for name in sorted(required - names))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true")
    parser.add_argument("--package", type=Path)
    parser.add_argument("--private-reference", type=Path,
                        default=ROOT / ".deps/google-client.json",
                        help="Compare private OAuth values without printing them")
    args = parser.parse_args()
    if args.private_reference.is_file():
        reference = json.loads(args.private_reference.read_text(encoding="utf-8-sig"))
        client = reference.get("installed", reference.get("web", {}))
        for field in ("client_id", "client_secret"):
            value = client.get(field, "")
            if isinstance(value, str) and len(value) >= 12:
                PRIVATE_VALUES.append(("private reference " + field, value.encode()))
    failures = audit_worktree()
    if args.history:
        failures += audit_history()
    if args.package:
        failures += audit_package(args.package)
    for location, label in failures:
        print(f"FAIL: {label}: {location}")  # Deliberately no matched values.
    if failures:
        return 1
    print("Credential-pattern and package checks passed (not a complete security audit).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
