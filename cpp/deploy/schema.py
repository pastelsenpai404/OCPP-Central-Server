#!/usr/bin/env python3
"""Check this dedicated database, optionally apply reviewed additive migrations."""
import json
from pathlib import Path
import re
import subprocess
import sys

root, mode = Path(sys.argv[1]), sys.argv[2]
config = json.loads(Path('/etc/ocpp-cpp/config.local.json').read_text(encoding='utf-8-sig'))
if config['OCPP_DB_NAME'] != 'ocpp_cpp':
    raise SystemExit('Deployment supports the dedicated ocpp_cpp database only')
client = ['/opt/ocpp-db/runtime/usr/bin/mariadb', '--no-defaults',
          '--socket=/run/ocpp-db/mysql.sock', '-u', 'root', '-N', '-B']
tables = set()
for migration in sorted((root / 'migrations').glob('*.sql')):
    sql = re.sub(r'--[^\n]*', '', migration.read_text(encoding='utf-8')).strip()
    statements = [s.strip() for s in sql.split(';') if s.strip()]
    for statement in statements:
        match = re.match(r'CREATE TABLE IF NOT EXISTS ([a-z0-9_]+)\s*\(', statement)
        if not match:
            raise SystemExit('Migration requires explicit review outside this additive deploy tool')
        tables.add(match[1])
    if mode == 'apply':
        subprocess.run(client + ['ocpp_cpp'], input=sql, text=True, check=True)
actual = subprocess.check_output(client + ['-e', "SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA='ocpp_cpp'"], text=True).splitlines()
missing = tables - set(actual)
if missing:
    raise SystemExit('Missing additive schema tables; run deploy.ps1 -ApplyMigrations after review')
print(f'PASS: {len(tables)} additive runtime tables present')
