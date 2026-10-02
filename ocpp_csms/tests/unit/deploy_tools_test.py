"""Protect secret serialization and fail-closed deployment config generation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class DeploymentConfigTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / 'config.json'
        self.output = self.directory / 'runtime.env'
        self.config = dict(OCPP_DB_NAME='ocpp_cpp', OCPP_DB_USER='app',
                           OCPP_DB_PASSWORD='quote" slash\\ dollar$ single\' unicode\u0e01',
                           OCPP_READ_TOKEN='1' * 64, OCPP_OPERATOR_TOKEN='2' * 64,
                           OCPP_ADMIN_TOKEN='3' * 64,
                           OCPP_STATION_SECRETS={'CP01': '4' * 64}, OCPP_DB_PORT=13307)

    def run_tool(self, domain='ocpp.example.test'):
        self.source.write_text(json.dumps(self.config), encoding='utf-8')
        return subprocess.run([sys.executable, str(ROOT / 'deploy/runtime-env.py'),
                               str(self.source), str(self.output), domain, '5003'],
                              capture_output=True, text=True)

    def test_escaped_secret_and_deployment_overrides(self):
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        content = self.output.read_text(encoding='utf-8')
        self.assertIn('OCPP_DB_PASSWORD="quote\\" slash\\\\ dollar$ single\' unicode\u0e01"', content)
        self.assertIn('OCPP_DB_PORT="3307"', content)
        self.assertIn('OCPP_PUBLIC_ORIGIN="https://ocpp.example.test"', content)
        self.assertIn('OCPP_STATION_SECRETS="{\\"CP01\\":\\"', content)
        self.assertEqual(result.stdout, '')
        if os.name != 'nt':
            self.assertEqual(self.output.stat().st_mode & 0o777, 0o600)

    def test_newline_in_secret_rejected(self):
        self.config['OCPP_DB_PASSWORD'] = 'secret\nOCPP_ADMIN_TOKEN=attacker'
        result = self.run_tool()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())
        self.assertNotIn('secret', result.stderr)

    def test_missing_credential_rejected(self):
        del self.config['OCPP_ADMIN_TOKEN']
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_hostname_shell_injection_rejected(self):
        self.assertNotEqual(self.run_tool('ocpp.example.test;id').returncode, 0)
        self.assertFalse(self.output.exists())

if __name__ == '__main__':
    unittest.main()
