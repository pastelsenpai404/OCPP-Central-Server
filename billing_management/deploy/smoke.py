"""Readiness and sandbox-only auth/CORS/input checks. Never prints credentials."""
import argparse
import json
from pathlib import Path
import urllib.error
import urllib.request
from configure import domains


def check(base, directory, tls=False):
    environments = {}
    for area in domains(base):
        environments[area] = dict(line.split('=', 1) for line in
                                 (directory / f'{area}.env').read_text().splitlines()
                                 if line and not line.startswith('#'))
    count = 0
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    for area, (frontend, api, port) in domains(base).items():
        root = f'https://{api}' if tls else f'http://127.0.0.1:{port}'
        secret = environments[area]['BILLING_API_TOKEN']

        def request(path, expected, body=None, authorization=None, origin=None,
                    host=api, content_type='application/json', method=None):
            nonlocal count
            headers = {'Host': host, 'Content-Type': content_type}
            if authorization is not None:
                headers['Authorization'] = 'Bearer ' + authorization
            if origin is not None:
                headers['Origin'] = origin
            data = None if body is None else body.encode()
            req = urllib.request.Request(root + path, data=data, headers=headers, method=method)
            try:
                response = opener.open(req, timeout=5)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                if response.status != expected:
                    raise AssertionError(f'{area}: expected {expected}, got {response.status}')
                payload = response.read()
                count += 1
                return payload, response.headers

        payload, _ = request('/health/ready', 200)
        assert json.loads(payload)['mode'] == 'sandbox'
        body = '{"energy_wh":1500,"satang_per_kwh":750}'
        request('/api/v1/quote', 401, body)
        request('/api/v1/quote', 401, body, '0' * 64)
        other = 'customer' if area == 'management' else 'management'
        request('/api/v1/quote', 401, body, environments[other]['BILLING_API_TOKEN'])
        payload, headers = request('/api/v1/quote', 200, body, secret, f'https://{frontend}')
        assert json.loads(payload)['subtotal_satang'] == 1125
        assert headers['Access-Control-Allow-Origin'] == f'https://{frontend}'
        request('/api/v1/quote', 403, body, secret, 'https://denied.example')
        # Nginx rejects a mismatched Host by closing the connection (444).
        # Check the backend's HTTP 400 only on the direct loopback transport.
        if not tls:
            request('/api/v1/quote', 400, body, secret, host='denied.example')
        request('/api/v1/quote', 415, body, secret, content_type='text/plain')
        request('/api/v1/quote', 400, '{"energy_wh":-1,"satang_per_kwh":750}', secret)
        request('/api/v1/quote', 400, '{"energy_wh":1500.5,"satang_per_kwh":750}', secret)
        request('/api/v1/quote', 400, '{"energy_wh":1,"energy_wh":2,"satang_per_kwh":750}', secret)
        request('/api/v1/quote', 400, '{"energy_wh":1,"satang_per_kwh":750,"extra":1}', secret)
        request('/api/v1/quote', 413, ' ' * 5000, secret)
        request('/unknown', 404)
    print(f'{count} billing API smoke checks passed (sandbox only).')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('base_domain')
    parser.add_argument('environment_directory', type=Path)
    parser.add_argument('--tls', action='store_true')
    args = parser.parse_args()
    check(args.base_domain, args.environment_directory, args.tls)
