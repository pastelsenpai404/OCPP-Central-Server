"""Retry only Certbot lock contention, including automatic renewal jobs."""
import subprocess
import sys
import time

LOCK_ERROR = 'Another instance of Certbot is already running.'


def run(arguments, execute=subprocess.run, sleep=time.sleep, clock=time.monotonic,
        timeout=600, delay=5):
    deadline = clock() + timeout
    while True:
        result = execute(['/usr/bin/certbot', *arguments], stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
        if result.stdout:
            print(result.stdout, end='', flush=True)
        if result.returncode == 0 or LOCK_ERROR not in (result.stdout or ''):
            return result.returncode
        remaining = deadline - clock()
        if remaining <= 0:
            print('Certbot remained busy for 10 minutes; deployment will roll back.',
                  file=sys.stderr, flush=True)
            return result.returncode
        print('Certbot is busy; waiting before retrying HTTPS issuance...', flush=True)
        sleep(min(delay, remaining))


if __name__ == '__main__':
    raise SystemExit(run(sys.argv[1:]))
