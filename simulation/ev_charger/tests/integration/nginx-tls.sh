#!/usr/bin/env bash
# Isolated test nginx, trusted temporary certificate, no changes to public nginx.
set -euo pipefail
umask 077
stage=${1:?private validation directory}
[[ $stage == /var/tmp/ev-simulation-tls-* && -d $stage ]]
for port in 9080 9443; do [[ -z $(ss -H -lnt "sport = :$port") ]] || { echo 'Validation port occupied.' >&2; exit 1; }; done
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=ev.simulation.barryofeverything.com \
  -addext 'subjectAltName=DNS:ev.simulation.barryofeverything.com' -keyout "$stage/key.pem" -out "$stage/cert.pem" >/dev/null 2>&1
python3 /opt/ev-simulation/current/deploy/configure.py nginx-tls barryofeverything.com /opt/ev-simulation/current > "$stage/sites.conf"
python3 - "$stage" <<'PY'
from pathlib import Path
import sys
stage=Path(sys.argv[1]);path=stage/'sites.conf';text=path.read_text()
text=text.replace('listen 80;','listen 127.0.0.1:9080;').replace('listen [::]:80;','')
text=text.replace('listen 443 ssl;','listen 127.0.0.1:9443 ssl;').replace('listen [::]:443 ssl;','')
text=text.replace('/etc/letsencrypt/live/ev-simulation-barryofeverything.com/fullchain.pem',str(stage/'cert.pem')).replace('/etc/letsencrypt/live/ev-simulation-barryofeverything.com/privkey.pem',str(stage/'key.pem'))
path.write_text(text)
(stage/'nginx.conf').write_text(f'pid {stage}/nginx.pid;\nerror_log {stage}/error.log;\nevents {{worker_connections 128;}}\nhttp {{include /etc/nginx/mime.types; access_log off; include {path};}}\n')
PY
nginx -t -c "$stage/nginx.conf" -p "$stage"
nginx -c "$stage/nginx.conf" -p "$stage"
trap 'nginx -s quit -c "$stage/nginx.conf" -p "$stage" >/dev/null 2>&1 || true' EXIT
python3 - "$stage" <<'PY'
import socket,ssl,sys,runpy,urllib.request
from pathlib import Path
stage=Path(sys.argv[1]);host='ev.simulation.barryofeverything.com';original=socket.getaddrinfo
socket.getaddrinfo=lambda h,p,*a,**k:original('127.0.0.1',9443,*a,**k) if h==host else original(h,p,*a,**k)
ssl._create_default_https_context=lambda:ssl.create_default_context(cafile=str(stage/'cert.pem'))
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
sys.argv=['smoke.py','barryofeverything.com','/etc/ev-simulation','--tls']
runpy.run_path('/opt/ev-simulation/current/deploy/smoke.py',run_name='__main__')
with urllib.request.urlopen('https://'+host+'/') as response:
    assert 'EV Simulation' in response.read().decode()
    assert "'sha256-" in response.headers['Content-Security-Policy']
with urllib.request.urlopen('https://'+host+'/wasm/physics.wasm') as response:
    assert response.read(4)==b'\0asm'
    assert response.headers['Content-Type']=='application/wasm'
print('Isolated nginx TLS static/API routing, CSP hashes and WASM MIME passed')
PY
