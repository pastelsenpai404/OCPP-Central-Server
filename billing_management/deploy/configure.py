"""Generate root-only environment files and domain-specific Nginx configuration."""
import base64
import hashlib
import os
from pathlib import Path
import re
import secrets
import shlex
import sys


def domains(base):
    if not re.fullmatch(r'[a-z0-9]+(?:[.-][a-z0-9]+)*\.[a-z]{2,}', base) or len(base) > 180:
        raise ValueError('Invalid base domain')
    return {
        'management': (f'ev.admin.{base}', f'ev.admin.api.{base}', 5500),
        'customer': (f'ev.customer.{base}', f'ev.customer.api.{base}', 5501),
    }


def environment(directory, base, ocpp_environment=Path('/etc/ocpp-cpp/runtime.env')):
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    tokens = []
    ocpp_values = {}
    if ocpp_environment.is_file():
        ocpp_values = dict(line.split('=', 1) for line in ocpp_environment.read_text().splitlines()
                           if line and not line.startswith('#'))
        for key in ('OCPP_ADMIN_TOKEN', 'OCPP_PORT'):
            if key in ocpp_values:
                parts = shlex.split(ocpp_values[key])
                if len(parts) != 1:
                    raise ValueError('Invalid quoted OCPP environment value')
                ocpp_values[key] = parts[0]
    for area, (frontend, api, port) in domains(base).items():
        target = directory / f'{area}.env'
        token = None
        values = {}
        if target.exists():
            values = dict(line.split('=', 1) for line in target.read_text().splitlines()
                          if line and not line.startswith('#'))
            token = values.get('BILLING_API_TOKEN')
            if not token or not re.fullmatch(r'[a-f0-9]{64}', token):
                raise ValueError('Invalid existing API token; refusing to rotate implicitly')
        token = token or secrets.token_hex(32)
        tokens.append(token)
        if len(set(tokens)) != len(tokens):
            raise ValueError('Management/customer tokens must be distinct')
        temporary = directory / f'{area}.env.next'
        descriptor = os.open(temporary, os.O_CREAT | os.O_TRUNC | os.O_WRONLY, 0o600)
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(f'BILLING_PORT={port}\nBILLING_API_HOST={api}\n'
                         f'BILLING_ALLOWED_ORIGIN=https://{frontend}\nBILLING_API_TOKEN={token}\n')
            if area == 'management':
                stream.write('BILLING_DATABASE_PATH=/var/lib/billing-management/backoffice.sqlite3\n')
                for name in ('BILLING_READER_TOKEN', 'BILLING_OPERATOR_TOKEN'):
                    role_token = values.get(name) if target.exists() else None
                    if role_token and (not re.fullmatch(r'[a-f0-9]{64}', role_token) or role_token in tokens):
                        raise ValueError('Invalid role token')
                    role_token = role_token or secrets.token_hex(32)
                    tokens.append(role_token)
                    stream.write(f'{name}={role_token}\n')
                ocpp_token = ocpp_values.get('OCPP_ADMIN_TOKEN', values.get('BILLING_OCPP_TOKEN', ''))
                if ocpp_token:
                    if not re.fullmatch(r'[a-f0-9]{64}', ocpp_token):
                        raise ValueError('Invalid OCPP integration token')
                    ocpp_port = ocpp_values.get('OCPP_PORT', values.get('BILLING_OCPP_PORT', '5003'))
                    if not ocpp_port.isdigit() or not 1024 <= int(ocpp_port) <= 65535:
                        raise ValueError('Invalid OCPP integration port')
                    stream.write(f'BILLING_OCPP_TOKEN={ocpp_token}\nBILLING_OCPP_HOST=ocpp.{base}\n'
                                 f'BILLING_OCPP_PORT={ocpp_port}\n')
        temporary.chmod(0o600)
        temporary.replace(target)


def nginx(base, release, tls):
    parts = ['# Managed by billing_management/deploy.ps1\n']
    if tls:
        parts += ['limit_req_zone "$server_name:$binary_remote_addr" zone=billing_api:10m rate=10r/s;\n']
    for area, (frontend, api, port) in domains(base).items():
        for host in (frontend, api):
            parts.append(f'''server {{
    listen 80;
    listen [::]:80;
    server_name {host};
    if ($host != {host}) {{ return 444; }}
    location ^~ /.well-known/acme-challenge/ {{
        root /var/www/billing-acme;
        default_type text/plain;
        try_files $uri =404;
    }}
    location / {{ {'return 308 https://' + host + '$request_uri;' if tls else 'return 503;'} }}
}}
''')
        if not tls:
            continue
        certificate = f'/etc/letsencrypt/live/billing-management-{base}'
        for host in (frontend, api):
            parts.append(f'''server {{
    listen 443 ssl;
    listen [::]:443 ssl;
    server_name {host};
    if ($host != {host}) {{ return 444; }}
    ssl_certificate {certificate}/fullchain.pem;
    ssl_certificate_key {certificate}/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_session_cache shared:BillingTLS:5m;
    ssl_session_tickets off;
    server_tokens off;
    client_max_body_size 4k;
    client_header_timeout 10s;
    client_body_timeout 10s;
    add_header Strict-Transport-Security "max-age=31536000" always;
    add_header X-Content-Type-Options nosniff always;
    add_header Referrer-Policy same-origin always;
''')
            if host == frontend:
                html = (release / 'web' / area / 'index.html').read_text(encoding='utf-8')
                hashes = []
                for attributes, script in re.findall(r'<script\b([^>]*)>(.*?)</script>', html, re.S):
                    if re.search(r'\bsrc\s*=', attributes):
                        continue
                    digest = base64.b64encode(hashlib.sha256(script.encode()).digest()).decode()
                    hashes.append(f"'sha256-{digest}'")
                policy = ("default-src 'self'; script-src 'self' 'wasm-unsafe-eval' " + ' '.join(hashes) +
                          f"; style-src 'self' 'unsafe-inline'; connect-src 'self' https://{api}; "
                          "object-src 'none'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'")
                parts.append(f'''    root /opt/billing-management/current/web/{area};
    index index.html;
    add_header Content-Security-Policy "{policy}" always;
    location / {{ try_files $uri $uri/index.html =404; }}
    location ~ (^|/)\\. {{ return 404; }}
}}
''')
            else:
                parts.append(f'''    location / {{
        limit_req zone=billing_api burst=20 nodelay;
        limit_req_status 429;
        proxy_pass http://127.0.0.1:{port};
        proxy_http_version 1.1;
        proxy_set_header Host {api};
        proxy_set_header Connection "";
        proxy_set_header X-Forwarded-For $remote_addr;
        proxy_set_header X-Forwarded-Proto https;
        proxy_set_header Forwarded "";
        proxy_connect_timeout 3s;
        proxy_read_timeout 10s;
        proxy_send_timeout 10s;
    }}
}}
''')
    return ''.join(parts)


if __name__ == '__main__':
    operation, base, path = sys.argv[1:4]
    if operation == 'environment':
        environment(Path(path), base)
    elif operation in ('nginx-http', 'nginx-tls'):
        print(nginx(base, Path(path), operation == 'nginx-tls'))
    else:
        raise SystemExit('Unknown operation')
