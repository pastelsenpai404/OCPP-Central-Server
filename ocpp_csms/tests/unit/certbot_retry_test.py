import contextlib
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('certbot_retry', ROOT / 'deploy/certbot.py')
certbot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(certbot)


class CertbotRetryTest(unittest.TestCase):
    def execute(self, replies, timeout=10):
        calls, pauses = [], []
        elapsed = [0]

        def invoke(command, **options):
            calls.append(command)
            code, output = replies[min(len(calls) - 1, len(replies) - 1)]
            return SimpleNamespace(returncode=code, stdout=output)

        def sleep(seconds):
            pauses.append(seconds)
            elapsed[0] += seconds

        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = certbot.run(['certonly', '-d', 'ocpp.example.com'], invoke, sleep,
                                 lambda: elapsed[0], timeout=timeout, delay=3)
        return result, calls, pauses

    def test_lock_contention_then_success(self):
        result, calls, pauses = self.execute([(1, certbot.LOCK_ERROR), (1, certbot.LOCK_ERROR), (0, 'Certificate saved')])
        self.assertEqual(result, 0)
        self.assertEqual(pauses, [3, 3])
        self.assertEqual(calls, [['/usr/bin/certbot', 'certonly', '-d', 'ocpp.example.com']] * 3)

    def test_acme_errors_are_not_retried(self):
        result, calls, pauses = self.execute([(7, 'Some challenges have failed.')])
        self.assertEqual((result, len(calls), pauses), (7, 1, []))

    def test_lock_wait_is_bounded(self):
        result, calls, pauses = self.execute([(1, certbot.LOCK_ERROR)], timeout=5)
        self.assertEqual(result, 1)
        self.assertEqual(pauses, [3, 2])
        self.assertEqual(len(calls), 3)

    def test_all_deployers_ship_same_helper(self):
        expected = (ROOT / 'deploy/certbot.py').read_bytes()
        for product in ('ocpp_csms', 'billing_management', 'simulation/ev_charger'):
            deploy = ROOT.parent / product / 'deploy'
            self.assertEqual((deploy / 'certbot.py').read_bytes(), expected)
            self.assertIn('python3 "$release/deploy/certbot.py" certonly',
                          (deploy / 'remote-deploy.sh').read_text())


if __name__ == '__main__':
    unittest.main()
