"""Isolated Windows OBS recording test; never opens the user's OBS profile.

Pass --inspect to keep the test instance open for UI review after recording.
Only generated files beneath artifacts/windows-smoke-* are created.
"""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import subprocess
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("stage", type=Path)
parser.add_argument("--obs", type=Path, default=Path(r"C:\Program Files\obs-studio"))
parser.add_argument("--language", choices=["en-US", "es-ES"], default="en-US")
parser.add_argument("--inspect", action="store_true")
args = parser.parse_args()
stage = args.stage.resolve()
assert (stage / "broadcast-scheduler/bin/64bit/broadcast-scheduler.dll").is_file()
root = Path(tempfile.mkdtemp(prefix="windows-smoke-", dir=ROOT / "artifacts"))
runtime = root / "obs"
excluded = shutil.ignore_patterns("*.pdb", "broadcast-scheduler*",
                                 "Qt6HttpServer.dll", "Qt6WebSockets.dll", "tls")
for folder in ("bin", "data", "obs-plugins"):
    shutil.copytree(args.obs / folder, runtime / folder, ignore=excluded)
config = runtime / "config/obs-studio"
shutil.copytree(ROOT / "tests/fixtures/obs-studio", config)
for name in ("global.ini", "user.ini"):
    path = config / name
    path.write_text(path.read_text(encoding="utf-8").replace(
        "[General]", f"[General]\nLanguage={args.language}"), encoding="utf-8")
recordings = root / "recordings"
recordings.mkdir()
profile = config / "basic/profiles/SchedulerSmoke/basic.ini"
profile.write_text(profile.read_text(encoding="utf-8").replace(
    "/tmp/bs-recordings", recordings.as_posix()), encoding="utf-8")
scenes = config / "basic/scenes"
scenes.mkdir(parents=True, exist_ok=True)
(scenes / "Untitled.json").write_text(json.dumps({
    "name": "Untitled", "current_scene": "Test scene",
    "current_program_scene": "Test scene", "scene_order": [{"name": "Test scene"}],
    "sources": [{"name": "Test scene", "id": "scene", "settings": {"items": []}}]
}), encoding="utf-8")
data_root = root / "plugin-data"
shutil.copytree(stage / "broadcast-scheduler/data", data_root / "broadcast-scheduler")
with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
token = "native-smoke-test-only"
env = {**os.environ,
       "PATH": str(ROOT / ".deps/qt6/bin") + os.pathsep + os.environ["PATH"],
       "BS_TEST_TOKEN": token, "BS_TEST_PORT": str(port)}
db = config / "plugin_config/broadcast-scheduler/scheduler.sqlite3"
subprocess.run([str(ROOT / "build-windows/scheduler-service.exe"), str(db), "--initialize"],
               env=env, check=True, timeout=30)
# Keep SDK DLLs and old global TLS backends out of the OBS process search path.
env = {**os.environ, "QT_PLUGIN_PATH": "", "QT_QPA_PLATFORM_PLUGIN_PATH": "",
       "OBS_PLUGINS_PATH": (stage / "%module%/bin/64bit").as_posix(),
       "OBS_PLUGINS_DATA_PATH": data_root.as_posix()}
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
process = subprocess.Popen(
    [str(runtime / "bin/64bit/obs64.exe"), "--portable", "--multi",
     "--disable-updater", "--disable-missing-files-check"],
    cwd=runtime / "bin/64bit", env=env, startupinfo=startup)
(root / "test.json").write_text(json.dumps({
    "pid": process.pid, "port": port, "root": str(root),
    "language": args.language}), encoding="utf-8")
print(f"Isolated OBS PID {process.pid}; test data: {root}", flush=True)


def request(method, path, body=None):
    req = urllib.request.Request(f"http://127.0.0.1:{port}/api/v1{path}",
        data=None if body is None else json.dumps(body).encode(),
        headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"},
        method=method)
    with urllib.request.urlopen(req, timeout=3) as response:
        return json.loads(response.read())


def wait_for(predicate, seconds=45):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("Isolated OBS exited before completing the test")
        try:
            if predicate():
                return
        except (OSError, KeyError):
            pass
        time.sleep(.2)
    raise AssertionError("Timed out waiting for isolated OBS")


try:
    wait_for(lambda: request("GET", "/status")["version"] == "0.2.2", 60)
    start = dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=5)
    event = request("POST", "/events", {
        "title": "Community release test", "start": start.isoformat(),
        "end": (start + dt.timedelta(seconds=4)).isoformat()})
    wait_for(lambda: request("GET", "/status")["obs"]["recording"])
    wait_for(lambda: not request("GET", "/status")["obs"]["recording"])
    with sqlite3.connect(db) as conn:
        actions = conn.execute(
            "SELECT action,result FROM executions WHERE event_id=?", (event["id"],)).fetchall()
    assert sorted(actions) == [("record.start", "succeeded"), ("record.stop", "succeeded")], actions
    files = list(recordings.glob("*Community release test*.mkv"))
    assert files and files[0].stat().st_size > 1000, "No recorded media with event filename"
    assert files[0].name.startswith(start.astimezone().strftime("%Y-%m-%d")), files[0].name
    for days, title in [(1, "Workshop — test calendar"), (2, "Community livestream rehearsal")]:
        upcoming = start + dt.timedelta(days=days)
        request("POST", "/events", {"title": title, "start": upcoming.isoformat(),
                "end": (upcoming + dt.timedelta(hours=1)).isoformat()})
    print("PASS: real OBS start/stop, confirmed history, event-based recording filename", flush=True)
    if args.inspect:
        print("Close this isolated OBS window after UI review.", flush=True)
        process.wait()
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=20)
