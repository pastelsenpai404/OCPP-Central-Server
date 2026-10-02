"""Run real API executables with temporary sandbox credentials, then stop them."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'deploy'))
from configure import environment
from smoke import check

build = Path(sys.argv[1]).resolve()
processes = []
with tempfile.TemporaryDirectory(prefix='billing-http-') as temporary:
    directory = Path(temporary)
    environment(directory, 'example.com')
    try:
        for area in ('management', 'customer'):
            env = os.environ.copy()
            env.update(dict(line.split('=', 1) for line in (directory / f'{area}.env').read_text().splitlines()))
            binary = build / area / 'backend'
            if os.name == 'nt':
                binary = binary / 'Release' / f'billing_{area}_api.exe'
            else:
                binary = binary / f'billing_{area}_api'
            process = subprocess.Popen([str(binary)], env=env, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            processes.append(process)
            for _ in range(100):
                if process.poll() is not None:
                    raise AssertionError('API process exited during startup')
                req = urllib.request.Request(f'http://127.0.0.1:{env["BILLING_PORT"]}/health/ready',
                                             headers={'Host': env['BILLING_API_HOST']})
                try:
                    with urllib.request.urlopen(req, timeout=.3) as response:
                        if response.status == 200:
                            break
                except (OSError, urllib.error.HTTPError):
                    time.sleep(.1)
            else:
                raise AssertionError('API never became ready')
        check('example.com', directory)
    finally:
        for process in processes:
            process.terminate()
        for process in processes:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
