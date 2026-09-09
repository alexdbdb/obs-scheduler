"""Load the packaged DLL against the installed OBS runtime without starting OBS."""
import ctypes
import os
from pathlib import Path
import sys

stage = Path(sys.argv[1]).resolve()
obs = Path(sys.argv[2]).resolve()
with os.add_dll_directory(str(obs / 'bin' / '64bit')), os.add_dll_directory(str(stage / 'bin' / '64bit')):
    websockets = ctypes.CDLL(str(stage / 'bin' / '64bit' / 'Qt6WebSockets.dll'))
    http = ctypes.CDLL(str(stage / 'bin' / '64bit' / 'Qt6HttpServer.dll'))
    plugin = ctypes.CDLL(str(stage / 'obs-plugins' / '64bit' / 'broadcast-scheduler.dll'))
    plugin.obs_module_name.restype = ctypes.c_char_p
    assert plugin.obs_module_name() == b'Broadcast Scheduler'
    plugin.obs_module_ver.restype = ctypes.c_uint32
    assert plugin.obs_module_ver() >> 24 == 32
    print('Packaged plugin loaded successfully against installed OBS 32 runtime')
