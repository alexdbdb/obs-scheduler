"""Package exact committed project and dependency sources for binary releases."""
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / ".deps"
VERSION = "0.2.0"


def git(*arguments, cwd=ROOT):
    return subprocess.check_output(["git", *arguments], cwd=cwd).decode().strip()


def main():
    if git("status", "--porcelain"):
        raise SystemExit("Commit reviewed changes before packaging corresponding source")
    subprocess.run(["python", str(ROOT / "scripts/release-audit.py"), "--history"],
                   cwd=ROOT, check=True)
    cache = DEPS / "source-distribution"
    cache.mkdir(exist_ok=True)
    artifacts = ROOT / "artifacts"
    artifacts.mkdir(exist_ok=True)
    project_commit = git("rev-parse", "HEAD")
    project_archive = cache / f"broadcast-scheduler-{project_commit}.zip"
    subprocess.run(["git", "archive", "--format=zip",
                    "--prefix=broadcast-scheduler/", f"--output={project_archive}", "HEAD"],
                   cwd=ROOT, check=True)
    sources = [("broadcast-scheduler.zip", project_archive)]
    manifest = {"version": VERSION, "project_commit": project_commit, "dependencies": []}
    dependencies = [
        ("qthttpserver", "qt/qthttpserver", "v6.11.1"),
        ("qtwebsockets", "qt/qtwebsockets", "v6.11.1"),
        ("qtbase-test", "qt/qtbase", "v6.11.1"),
        ("libical", "libical/libical", "v3.0.20"),
    ]
    for directory, origin, tag in dependencies:
        checkout = DEPS / directory
        if git("status", "--porcelain", "--untracked-files=no", cwd=checkout):
            raise SystemExit(f"Dependency has modified tracked files: {directory}")
        commit = git("rev-parse", "HEAD", cwd=checkout)
        if commit != git("rev-parse", tag + "^{commit}", cwd=checkout):
            raise SystemExit(f"Dependency checkout does not match {tag}: {directory}")
        name = origin.split("/")[1]
        archive = cache / f"{name}-{commit}.zip"
        if not archive.exists():
            if name == "qtbase":
                # The build uses a sparse Qt checkout; fetch the full source by
                # its exact commit without populating gigabytes of build files.
                url = f"https://codeload.github.com/{origin}/zip/{commit}"
                temporary = archive.with_suffix(".download")
                print(f"Downloading corresponding source: {origin} at {commit}", flush=True)
                with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as output:
                    while block := response.read(1024 * 1024):
                        output.write(block)
                with zipfile.ZipFile(temporary) as verify:
                    if verify.testzip():
                        raise RuntimeError("Invalid dependency source archive")
                temporary.replace(archive)
            else:
                subprocess.run(["git", "archive", "--format=zip", f"--prefix={name}/",
                                f"--output={archive}", "HEAD"], cwd=checkout, check=True)
        sources.append((f"{name}-{tag}.zip", archive))
        manifest["dependencies"].append({
            "name": name, "tag": tag, "commit": commit,
            "source": f"https://github.com/{origin}/tree/{commit}",
            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        })
    sqlite = DEPS / "sqlite.zip"
    with zipfile.ZipFile(sqlite) as archive:
        for name in ("sqlite3.c", "sqlite3.h", "sqlite3ext.h"):
            relative = f"sqlite-amalgamation-3500400/{name}"
            if archive.read(relative) != (DEPS / relative).read_bytes():
                raise SystemExit("SQLite source differs from its pinned archive")
    sources.append(("sqlite-amalgamation-3500400.zip", sqlite))
    manifest["dependencies"].append({
        "name": "sqlite", "version": "3.50.4",
        "source": "https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip",
        "sha256": hashlib.sha256(sqlite.read_bytes()).hexdigest(),
    })
    destination = artifacts / f"broadcast-scheduler-{VERSION}-sources.zip"
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_STORED) as archive:
        for name, source in sources:
            archive.write(source, name)
        archive.writestr("source-manifest.json", json.dumps(manifest, indent=2) + "\n")
    # One manifest covers all distributable assets; omit InstallerTest builds.
    packages = [
        artifacts / f"Broadcast-Scheduler-{VERSION}-Setup.exe",
        artifacts / f"broadcast-scheduler-{VERSION}-windows-x64.zip",
        destination,
    ]
    checksums = [
        hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name
        for path in packages if path.is_file()
    ]
    (artifacts / f"SHA256SUMS-{VERSION}.txt").write_text("\n".join(checksums) + "\n", encoding="ascii")
    print(f"Corresponding sources ready: {destination}")


if __name__ == "__main__":
    main()
