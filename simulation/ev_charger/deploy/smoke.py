"""Read-only deployment checks; do not create stations on an active simulator."""
import json
from pathlib import Path
import sys
import urllib.error
import urllib.request

def request(url,host,token=None,body=None,origin=None):
    headers={'Host':host}
    if token:headers['Authorization']='Bearer '+token
    if origin:headers['Origin']=origin
    if body is not None:headers['Content-Type']='application/json'
    req=urllib.request.Request(url,headers=headers,data=body)
    try:
        with urllib.request.urlopen(req,timeout=20) as response:return response.status,response.read()
    except urllib.error.HTTPError as error:return error.code,error.read()

if __name__=='__main__':
    base,config=sys.argv[1:3]
    values=dict(line.split('=',1) for line in (Path(config)/'runtime.env').read_text().splitlines() if line and not line.startswith('#'))
    host='ev.simulation.'+base
    url='https://'+host if '--tls' in sys.argv else 'http://127.0.0.1:5600'
    tests=[('/health/ready',None,None,None,200),('/api/v1/chargers',None,None,None,401),('/api/v1/chargers','0'*64,None,None,401),('/api/v1/chargers',values['SIM_ADMIN_TOKEN'],None,None,200),('/api/v1/events',values['SIM_ADMIN_TOKEN'],None,None,200),('/api/v1/chargers',values['SIM_ADMIN_TOKEN'],b'{}','https://wrong.example',403)]
    for path,token,body,origin,expected in tests:
        status,_=request(url+path,host,token,body,origin)
        if status!=expected:raise SystemExit(f'Smoke failed: {path} expected {expected}, got {status}')
    print('6 read-only readiness/auth/origin checks passed')
