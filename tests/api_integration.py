"""Integration tests against the real HTTP server/store; no OBS output claims."""
import datetime as dt
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request


class API(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            cls.port = sock.getsockname()[1]
        cls.process = subprocess.Popen(
            [sys.argv[1], str(Path(cls.tmp.name) / 'scheduler.sqlite3')],
            env={**os.environ, 'BS_TEST_TOKEN': 'integration-test-only', 'BS_TEST_PORT': str(cls.port)},
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        for _ in range(100):
            try:
                if cls.request('GET', '/status')[0] == 200:
                    return
            except OSError:
                pass
            time.sleep(.1)
        raise RuntimeError('API did not become ready')

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        cls.process.communicate(timeout=10)
        cls.tmp.cleanup()

    @classmethod
    def request(cls, method, path, body=None, token='integration-test-only', headers=None):
        h = {'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json', **(headers or {})}
        req = urllib.request.Request(f'http://127.0.0.1:{cls.port}/api/v1{path}',
            data=None if body is None else json.dumps(body).encode(), headers=h, method=method)
        try:
            response = urllib.request.urlopen(req, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, json.loads(response.read())

    def event(self):
        start = dt.datetime.now(dt.timezone.utc) + dt.timedelta(hours=1)
        return {'title': 'API test', 'start': start.isoformat(),
                'end': (start + dt.timedelta(hours=2)).isoformat()}

    def test_crud(self):
        code, created = self.request('POST', '/events', self.event())
        self.assertEqual(code, 201)
        path = '/events/' + created['id']
        self.assertEqual(self.request('GET', path)[1]['title'], 'API test')
        created['title'] = 'Edited'
        self.assertEqual(self.request('PUT', path, created)[0], 200)
        self.assertEqual(self.request('GET', path)[1]['title'], 'Edited')
        self.assertEqual(self.request('DELETE', path)[0], 200)
        self.assertEqual(self.request('GET', path)[0], 404)

    def test_auth(self):
        self.assertEqual(self.request('GET', '/status', token='bad')[0], 401)
        self.assertEqual(self.request('GET', '/status', headers={'Origin': 'https://example.org'})[0], 403)

    def test_validation(self):
        event = self.event()
        event['start'] = 'not-a-date'
        self.assertEqual(self.request('POST', '/events', event)[0], 400)
        self.assertEqual(self.request('PATCH', '/events', {})[0], 405)
        self.assertEqual(self.request('POST', '/events', {'title': 'Incomplete'})[0], 400)

    def test_sync(self):
        self.assertEqual(self.request('POST', '/sync', {})[0], 202)

    def test_device_local_times(self):
        start = dt.datetime.now() + dt.timedelta(hours=2)
        code, created = self.request('POST', '/events', {
            'title': 'Local time', 'start': start.isoformat(),
            'end': (start + dt.timedelta(hours=1)).isoformat(),
        })
        self.assertEqual(code, 201)
        actual = dt.datetime.fromisoformat(created['start'].replace('Z', '+00:00'))
        self.assertLess(abs((actual - start.astimezone()).total_seconds()), 1)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]], verbosity=2)
