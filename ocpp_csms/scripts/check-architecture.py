#!/usr/bin/env python3
"""Enforce module include boundaries without build tools or third party packages."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {
    'protocol': {'protocol', 'config'},
    'config': {'config', 'protocol', 'security', 'transport'},
    'security': {'security'},
    'runtime': {'runtime'},
    'persistence': {'persistence', 'config', 'protocol'},
    'billing': {'billing', 'protocol'},
    'transport': {'transport', 'protocol', 'config'},
    'application': {'application', 'protocol', 'persistence', 'security', 'billing', 'config'},
    'server': set(),
}
ALLOWED['server'] = set(ALLOWED)
errors = []
count = 0
for base in ('src', 'include/ocpp'):
    for path in sorted((ROOT / base).rglob('*')):
        if path.suffix not in ('.cpp', '.hpp'):
            continue
        relative = path.relative_to(ROOT / base)
        if relative == Path('main.cpp'):
            continue
        owner = relative.parts[0]
        if owner not in ALLOWED:
            errors.append(f'{path.relative_to(ROOT)}: unknown module {owner}')
            continue
        count += 1
        for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
            match = re.match(r'\s*#\s*include\s*["<]([^">]+)[">]', line)
            if not match:
                continue
            dependency = match[1]
            prefix = f'{path.relative_to(ROOT)}:{number}'
            if dependency.endswith('.cpp'):
                errors.append(f'{prefix}: include a header instead of implementation')
            if dependency.startswith('server/') and owner != 'server':
                errors.append(f'{prefix}: server internals belong to the transport adapters')
            if dependency.startswith('../'):
                errors.append(f'{prefix}: relative traversal bypasses module boundaries')
            if dependency.startswith('ocpp/'):
                module = dependency.split('/')[1]
                if module not in ALLOWED[owner]:
                    errors.append(f'{prefix}: {owner} cannot depend on {module}')
                if not (ROOT / 'include' / dependency).is_file():
                    errors.append(f'{prefix}: missing public header {dependency}')
            if dependency.startswith(('drogon/', 'trantor/')) and owner != 'server':
                errors.append(f'{prefix}: HTTP/WebSocket framework belongs in server')
            if dependency == 'mysql.h' and owner != 'persistence':
                errors.append(f'{prefix}: database driver belongs in persistence')
if errors:
    print('\n'.join(errors), file=sys.stderr)
    sys.exit(1)
print(f'Architecture boundaries passed ({count} C++ files).')
