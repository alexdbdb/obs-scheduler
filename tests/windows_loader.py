"""Exercise OBS's real loader with a clean runtime, without starting personal OBS."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

if sys.argv[1] == '--load':
    runtime, plugin_dir = map(Path, sys.argv[2:4])
    with os.add_dll_directory(str(runtime)):
        libobs = ctypes.CDLL(str(runtime / 'obs.dll'))
        libobs.os_dlopen.argtypes = [ctypes.c_char_p]
        libobs.os_dlopen.restype = ctypes.c_void_p
        dll = plugin_dir / 'bin' / '64bit' / 'broadcast-scheduler.dll'
        handle = libobs.os_dlopen(dll.as_posix().encode())
        assert handle, 'OBS could not load the self-contained plugin'
        plugin = ctypes.CDLL(str(dll), handle=handle)
        plugin.obs_module_name.restype = ctypes.c_char_p
        assert plugin.obs_module_name() == b'Broadcast Scheduler'
        # Validate UTF-8, escapes and every translation with OBS's own parser,
        # not just Python's interpretation of the locale files.
        libobs.text_lookup_create.argtypes = [ctypes.c_char_p]
        libobs.text_lookup_create.restype = ctypes.c_void_p
        libobs.text_lookup_getstr.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                             ctypes.POINTER(ctypes.c_char_p)]
        libobs.text_lookup_getstr.restype = ctypes.c_bool
        libobs.text_lookup_destroy.argtypes = [ctypes.c_void_p]
        for locale in ('en-US', 'es-ES'):
            locale_file = plugin_dir / 'data' / 'locale' / (locale + '.ini')
            lookup = libobs.text_lookup_create(locale_file.as_posix().encode())
            assert lookup, locale
            try:
                for line in locale_file.read_text(encoding='utf-8-sig').splitlines():
                    if not line or line.startswith(('#', ';')):
                        continue
                    key, value = line.split('=', 1)
                    output = ctypes.c_char_p()
                    assert libobs.text_lookup_getstr(lookup, key.encode(), ctypes.byref(output)), key
                    assert output.value.decode() == json.loads(value), (locale, key)
            finally:
                libobs.text_lookup_destroy(lookup)
    # Process exit releases every transitive DLL before the parent cleans up.
    sys.exit(0)

stage, obs, probe = map(lambda p: Path(p).resolve(), sys.argv[1:4])
plugin_dir = stage / 'broadcast-scheduler'
# Exclude the old globally installed modules so they cannot mask missing DLLs.
with tempfile.TemporaryDirectory(prefix='scheduler-loader-') as tmp:
    runtime = Path(tmp)
    excluded = {'qt6httpserver.dll', 'qt6websockets.dll'}
    for dll in (obs / 'bin' / '64bit').glob('*.dll'):
        if dll.name.lower() not in excluded:
            shutil.copy2(dll, runtime / dll.name)
    subprocess.run([sys.executable, __file__, '--load', str(runtime), str(plugin_dir)],
                   check=True, timeout=30)
    shutil.copy2(probe, runtime / probe.name)
    env = {**os.environ, 'PATH': str(runtime) + os.pathsep + os.environ['SystemRoot'] + '\\System32',
           'QT_PLUGIN_PATH': '', 'QT_QPA_PLATFORM_PLUGIN_PATH': ''}
    subprocess.run([str(runtime / probe.name), str(plugin_dir / 'data' / 'qt')],
                   env=env, cwd=runtime, check=True, timeout=30)
print('OBS loader and isolated Schannel TLS backend: passed')
