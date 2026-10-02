#!/usr/bin/env python3
"""Convert the existing private server JSON into a systemd EnvironmentFile."""
import json
import os
from pathlib import Path
import re
import sys

config_path, output_path, domain, port = sys.argv[1:]
if not re.fullmatch(r"[a-z0-9]+(?:[.-][a-z0-9]+)*", domain) or not 1 <= int(port) <= 65535:
    raise SystemExit("Invalid deployment hostname or port")
config = json.loads(Path(config_path).read_text(encoding="utf-8-sig"))
required = ("OCPP_DB_NAME", "OCPP_DB_USER", "OCPP_DB_PASSWORD", "OCPP_READ_TOKEN",
            "OCPP_OPERATOR_TOKEN", "OCPP_ADMIN_TOKEN", "OCPP_STATION_SECRETS")
if not isinstance(config, dict) or any(not config.get(key) for key in required):
    raise SystemExit("Existing server configuration is incomplete; no secrets were modified")
config.update(OCPP_DB_HOST="127.0.0.1", OCPP_DB_PORT=3307, OCPP_PORT=int(port),
              OCPP_WORKERS=2, OCPP_PUBLIC_ORIGIN="https://" + domain)
ca = config.get("OCPP_DB_CA", "")
if ca:
    # Certificates are public; copy to a readable path rather than exposing the DB TLS directory.
    ca_data = Path(ca).read_bytes()
    target = Path(output_path).parent / "db-ca.pem"
    target.write_bytes(ca_data)
    target.chmod(0o644)
    config["OCPP_DB_CA"] = str(target)
lines = []
for key, value in config.items():
    if not re.fullmatch(r"OCPP_[A-Z0-9_]+", key):
        raise SystemExit("Unexpected configuration key")
    text = json.dumps(value, separators=(",", ":"), ensure_ascii=True) if isinstance(value, (list, dict)) else str(value)
    if any(ord(char) < 32 or ord(char) == 127 for char in text):
        raise SystemExit("Control character in configuration")
    lines.append(key + '="' + text.replace('\\', '\\\\').replace('"', '\\"') + '"\n')
path = Path(output_path)
fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, "w", encoding="utf-8") as output:
    output.writelines(lines)
path.chmod(0o600)
