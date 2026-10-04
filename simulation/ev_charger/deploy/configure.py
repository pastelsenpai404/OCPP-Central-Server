"""Server-only credentials and same-origin static/API Nginx deployment."""
import base64
import hashlib
import os
from pathlib import Path
import re
import secrets
import sys

def host(base):
    if not re.fullmatch(r'[a-z0-9]+(?:[.-][a-z0-9]+)*\.[a-z]{2,}', base) or len(base)>180:
        raise ValueError('Invalid domain')
    return 'ev.simulation.'+base

def environment(directory, base):
    directory.mkdir(mode=0o700,parents=True,exist_ok=True)
    path=directory/'runtime.env'
    values={}
    if path.exists():
        values=dict(line.split('=',1) for line in path.read_text().splitlines() if line and not line.startswith('#'))
        if not re.fullmatch('[a-f0-9]{64}',values.get('SIM_ADMIN_TOKEN','')):
            raise ValueError('Existing token invalid; refusing implicit rotation')
    values.update(SIM_PORT='5600',SIM_API_HOST=host(base),SIM_PUBLIC_ORIGIN='https://'+host(base))
    values.setdefault('SIM_ADMIN_TOKEN',secrets.token_hex(32))
    values.setdefault('SIM_CSMS_URL','')
    # systemd strips one quoting level: JSON must be wrapped in single quotes.
    values.setdefault('SIM_STATION_SECRETS',"'{}'")
    values.setdefault('SIM_CA_FILE','')
    if any('\n' in v or '\r' in v for v in values.values()):raise ValueError('Invalid environment')
    temporary=directory/'runtime.env.next'
    fd=os.open(temporary,os.O_CREAT|os.O_WRONLY|os.O_TRUNC,0o600)
    with os.fdopen(fd,'w') as stream:
        stream.write(''.join(f'{k}={v}\n' for k,v in values.items()))
    temporary.chmod(0o600);temporary.replace(path)

def nginx(base, release, tls):
    domain=host(base)
    result=f'''# Managed by simulation/ev_charger/deploy.ps1
server {{
    listen 80;
    listen [::]:80;
    server_name {domain};
    if ($host != {domain}) {{ return 444; }}
    location ^~ /.well-known/acme-challenge/ {{ root /var/www/ev-simulation-acme; default_type text/plain; try_files $uri =404; }}
    location / {{ {'return 308 https://'+domain+'$request_uri;' if tls else 'return 503;'} }}
}}
'''
    if not tls:return result
    html=(release/'web/index.html').read_text(encoding='utf-8')
    hashes=[]
    for attributes,script in re.findall(r'<script\b([^>]*)>(.*?)</script>',html,re.S):
        if not re.search(r'\bsrc\s*=',attributes):hashes.append("'sha256-"+base64.b64encode(hashlib.sha256(script.encode()).digest()).decode()+"'")
    csp="default-src 'self'; script-src 'self' 'wasm-unsafe-eval' "+' '.join(hashes)+"; style-src 'self' 'unsafe-inline'; connect-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'"
    result+=f'''limit_req_zone "$server_name:$binary_remote_addr" zone=ev_simulation_api:10m rate=10r/s;
server {{
    listen 443 ssl;
    listen [::]:443 ssl;
    server_name {domain};
    if ($host != {domain}) {{ return 444; }}
    ssl_certificate /etc/letsencrypt/live/ev-simulation-{base}/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/ev-simulation-{base}/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_session_tickets off;
    server_tokens off;
    client_max_body_size 8k;
    client_header_timeout 10s;
    client_body_timeout 10s;
    add_header Strict-Transport-Security "max-age=31536000" always;
    add_header X-Content-Type-Options nosniff always;
    add_header Referrer-Policy same-origin always;
    add_header Content-Security-Policy "{csp}" always;
    root /opt/ev-simulation/current/web;
    index index.html;
    location ~ (^|/)\\. {{ return 404; }}
    location / {{ try_files $uri $uri/index.html =404; }}
'''
    for prefix in ('/api/','/health/'):
        result+=f'''    location ^~ {prefix} {{
        limit_req zone=ev_simulation_api burst=20 nodelay;
        limit_req_status 429;
        proxy_pass http://127.0.0.1:5600;
        proxy_http_version 1.1;
        proxy_set_header Host {domain};
        proxy_set_header Connection "";
        proxy_set_header X-Forwarded-For $remote_addr;
        proxy_set_header Forwarded "";
        proxy_connect_timeout 3s;
        proxy_read_timeout 20s;
        proxy_send_timeout 10s;
    }}
'''
    return result+'}\n'

if __name__=='__main__':
    operation,base,path=sys.argv[1:4]
    if operation=='environment':environment(Path(path),base)
    elif operation in ('nginx-http','nginx-tls'):print(nginx(base,Path(path),operation=='nginx-tls'))
    else:raise SystemExit('Unknown operation')
