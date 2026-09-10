"""Run only in the disposable Docker development container after installing the plugin.

Starts real OBS/Xvfb and exercises a scheduled native recording.
Writes isolated test config and generated media under /tmp; never uses a user OBS profile.
"""
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import signal
import sqlite3
import subprocess
import tempfile
import time
import urllib.request

repo = Path(__file__).resolve().parents[1]
artifacts = repo / 'artifacts'
artifacts.mkdir(exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='bs-obs-smoke-'))
config = root / 'config'
shutil.copytree(repo / 'tests/fixtures/obs-studio', config / 'obs-studio')
(config / 'obs-studio/basic/scenes').mkdir(parents=True, exist_ok=True)
recordings_dir = root / 'recordings'
recordings_dir.mkdir()
profile = config / 'obs-studio/basic/profiles/SchedulerSmoke/basic.ini'
profile.write_text(profile.read_text().replace('/tmp/bs-recordings', str(recordings_dir)))
db = config / 'obs-studio/plugin_config/broadcast-scheduler/scheduler.sqlite3'
token = 'native-smoke-test-only'
env = {**os.environ, 'XDG_CONFIG_HOME': str(config), 'LIBGL_ALWAYS_SOFTWARE': '1', 'DISPLAY': ':88'}
subprocess.run([str(repo / 'build/scheduler-service'), str(db), '--initialize'],
               env={**env, 'BS_TEST_TOKEN': token}, check=True)
processes = []
output = (artifacts / 'obs-smoke.log').open('w')


def request(method, path, data=None):
    req = urllib.request.Request('http://127.0.0.1:8766/api/v1' + path,
        data=None if data is None else json.dumps(data).encode(),
        headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'}, method=method)
    with urllib.request.urlopen(req, timeout=4) as response:
        return json.loads(response.read())


def wait_for(predicate, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (OSError, KeyError):
            pass
        time.sleep(.2)
    raise AssertionError('Timed out waiting for real OBS transition')


def scheduled(title, seconds=5):
    start = dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=10)
    return request('POST', '/events', {'title': title, 'start': start.isoformat(),
        'end': (start + dt.timedelta(seconds=seconds)).isoformat()})


try:
    xvfb = subprocess.Popen(['Xvfb', ':88', '-screen', '0', '1600x1000x24', '-ac', '-nolisten', 'tcp'], stdout=output, stderr=output)
    processes.append(xvfb)
    time.sleep(1)
    obs = subprocess.Popen(['obs', '--multi', '--disable-missing-files-check', '--disable-updater'], env=env, stdout=output, stderr=output)
    processes.append(obs)
    wait_for(lambda: request('GET', '/status')['version'] == '0.2.2', 60)
    event = scheduled('Native recording smoke')
    wait_for(lambda: request('GET', '/status')['obs']['recording'])
    subprocess.run(['import', '-window', 'root', str(artifacts / 'obs-dock.png')], env=env, check=True)
    wait_for(lambda: not request('GET', '/status')['obs']['recording'])
    with sqlite3.connect(db) as conn:
        rows = conn.execute('SELECT action,result FROM executions WHERE event_id=?', (event['id'],)).fetchall()
    assert sorted(rows) == [('record.start', 'succeeded'), ('record.stop', 'succeeded')], rows
    recordings = list(recordings_dir.glob('*.mkv'))
    assert recordings, 'OBS did not create a recording'
    media = max(recordings, key=lambda p: p.stat().st_mtime)
    probe = subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'json', str(media)])
    assert float(json.loads(probe)['format']['duration']) > 1
    print('PASS: real OBS dock, scheduled recording, media validation and confirmed history')
    print('Test data:', root)
finally:
    for process in reversed(processes):
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    output.close()
