#!/usr/bin/env python3
"""Read-only deployment checks. Never print credentials or response records."""
import json
import base64
import os
import socket
import ssl
from pathlib import Path
import sys
import urllib.error
import urllib.request

domain, port, config_path = sys.argv[1:4]
secure = sys.argv[4:] == ['--tls']
base_url = f'https://{domain}' if secure else f'http://127.0.0.1:{port}'
config = json.loads(Path(config_path).read_text(encoding="utf-8-sig"))
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def request(path, origin=None, host=domain):
    headers = {"Host": host, "Authorization": "Bearer " + config["OCPP_ADMIN_TOKEN"]}
    if origin:
        headers["Origin"] = origin
    req = urllib.request.Request(base_url + path, headers=headers)
    try:
        with opener.open(req, timeout=10) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as response:
        return response.code, response.read()
    except (OSError, urllib.error.URLError):
        return 0, b''

checks = [request("/health/ready")[0] == 200,
          request("/")[0] == 200,
          request("/api/v1/admin/session", "https://" + domain)[0] == 200,
          request("/api/v1/admin/session", "http://" + domain)[0] == 403,
          request("/api/v1/admin/session", "https://foreign.example")[0] == 403,
          request("/api/v1/admin/session", "https://" + domain, "foreign.example")[0] in (0, 403)]
# Use raw HTTP for Upgrade: urllib intentionally replaces Connection with close.
station = next(iter(config['OCPP_STATION_SECRETS']))
def websocket(prefix, authenticated=False):
    credential = base64.b64encode((station + ':' + config['OCPP_STATION_SECRETS'][station]).encode()).decode()
    authorization = f'Authorization: Basic {credential}\r\n' if authenticated else ''
    connection = socket.create_connection(('127.0.0.1', int(port)), timeout=10)
    if secure:
        connection = ssl.create_default_context().wrap_socket(connection, server_hostname=domain)
    with connection:
        connection.sendall((f'GET {prefix}{station} HTTP/1.1\r\nHost: {domain}\r\n' + authorization +
                            'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                            'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: MDEyMzQ1Njc4OWFiY2RlZg==\r\n'
                            'Sec-WebSocket-Protocol: ocpp1.6\r\n\r\n').encode())
        response = b''
        while b'\r\n\r\n' not in response and len(response) <= 65536:
            chunk = connection.recv(4096)
            if not chunk:
                break
            response += chunk
        code = int(response.split(b' ', 2)[1])
        if code == 101:
            mask = os.urandom(4)
            payload = b'\x03\xe8'
            connection.sendall(b'\x88\x82' + mask + bytes(value ^ mask[index % 4] for index, value in enumerate(payload)))
        return code

for prefix in ('/ocpp/', '/steve/websocket/CentralSystemService/', '/develop/websocket/CentralSystemService/'):
    checks.append(websocket(prefix) == 401)
# Complete a handshake and close without sending a mutating OCPP action.
checks.append(websocket('/ocpp/', True) == 101)
checks.append(request('/runtime-packages.txt')[0] == 404)
if not all(checks):
    raise SystemExit("Deployment checks failed at indexes: " + ','.join(str(index + 1) for index, ok in enumerate(checks) if not ok))
print(f"PASS: {len(checks)} deployment readiness, origin, WebSocket and filesystem isolation checks")
