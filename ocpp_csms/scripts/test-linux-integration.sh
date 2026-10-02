#!/usr/bin/env bash
# Ephemeral local MariaDB only. Requires mariadb-server/client and Python test dependencies.
set -euo pipefail
umask 077
root_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
test_dir=$(mktemp -d "$root_dir/.deps-linux-test.XXXXXXXX")
read -r db_port server_port < <(python3 - <<'PY'
import socket
a=socket.socket(); b=socket.socket()
a.bind(('127.0.0.1',0)); b.bind(('127.0.0.1',0))
print(a.getsockname()[1],b.getsockname()[1])
a.close(); b.close()
PY
)
export OCPP_TEST_DB_PASSWORD
OCPP_TEST_DB_PASSWORD=$(python3 -c 'import secrets; print(secrets.token_hex(32))')
db_pid=''
cleanup() {
  if [[ -n "$db_pid" ]] && kill -0 "$db_pid" 2>/dev/null; then
    kill "$db_pid"
    wait "$db_pid" || true
  fi
  unset OCPP_TEST_DB_PASSWORD
  # Keep the private directory for failure diagnostics; no recursive deletion.
}
trap cleanup EXIT
mariadb-install-db --no-defaults --datadir="$test_dir/data" --auth-root-authentication-method=normal --skip-test-db >"$test_dir/init.log" 2>&1
mariadbd --no-defaults --user="$(id -un)" --datadir="$test_dir/data" --socket="$test_dir/mysql.sock" \
  --pid-file="$test_dir/mysql.pid" --bind-address=127.0.0.1 --port="$db_port" --skip-name-resolve \
  --innodb-buffer-pool-size=64M >"$test_dir/server.log" 2>&1 &
db_pid=$!
ready=0
for attempt in {1..100}; do
  if ! kill -0 "$db_pid" 2>/dev/null; then echo 'Temporary database exited; inspect private test directory.' >&2; exit 1; fi
  if mariadb --no-defaults --socket="$test_dir/mysql.sock" -u root -e 'SELECT 1' >/dev/null 2>&1; then ready=1; break; fi
  sleep 0.1
done
if [[ "$ready" != 1 ]]; then echo 'Temporary database did not become ready.' >&2; exit 1; fi
python3 - <<'PY' | mariadb --no-defaults --socket="$test_dir/mysql.sock" -u root
import os
p=os.environ['OCPP_TEST_DB_PASSWORD']
assert len(p)==64 and all(c in '0123456789abcdef' for c in p)
print("CREATE USER IF NOT EXISTS 'root'@'127.0.0.1' IDENTIFIED BY '"+p+"';")
print("ALTER USER 'root'@'127.0.0.1' IDENTIFIED BY '"+p+"';")
print("GRANT ALL ON *.* TO 'root'@'127.0.0.1';")
PY
python3 "$root_dir/tests/integration/integration.py" --server "$root_dir/build/ocpp_server" --db-port "$db_port" --port "$server_port"
