import importlib.util
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('billing_configure', ROOT / 'deploy/configure.py')
configure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(configure)


class DeploymentConfigTest(unittest.TestCase):
    def test_invalid_domain_rejected(self):
        for base in ('bad;command.com', '../example.com', 'bad\nexample.com'):
            with self.assertRaises(ValueError):
                configure.domains(base)

    def test_tokens_preserved_and_roles_distinct(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            configure.environment(path, 'example.com')
            first = [(path / f'{area}.env').read_text() for area in ('management', 'customer')]
            configure.environment(path, 'example.com')
            second = [(path / f'{area}.env').read_text() for area in ('management', 'customer')]
            self.assertEqual(first, second)
            tokens = [re.search(r'BILLING_API_TOKEN=([a-f0-9]{64})', text).group(1) for text in first]
            self.assertNotEqual(*tokens)

    def test_tls_domains_and_inline_script_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            for area in ('management', 'customer'):
                web = path / 'web' / area
                web.mkdir(parents=True)
                (web / 'index.html').write_text('<script>const ready = true;</script>', encoding='utf-8')
            text = configure.nginx('example.com', path, True)
            for label in ('ev.admin', 'ev.admin.api', 'ev.customer', 'ev.customer.api'):
                self.assertIn(f'server_name {label}.example.com;', text)
            self.assertIn("'wasm-unsafe-eval'", text)
            self.assertIn("'sha256-", text)
            self.assertNotIn("script-src 'unsafe-inline'", text)
            self.assertIn('proxy_pass http://127.0.0.1:5500;', text)
            self.assertIn('proxy_pass http://127.0.0.1:5501;', text)


if __name__ == '__main__':
    unittest.main()
