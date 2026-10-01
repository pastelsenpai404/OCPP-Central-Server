#!/bin/sh
set -eu
umask 077
destination=/var/backups/ocpp-db
install -d -m 700 "$destination"
stamp=$(date -u +%Y%m%dT%H%M%SZ)
dump="$destination/ocpp_cpp-$stamp.sql"
trap 'rm -f "$dump.partial"' EXIT HUP INT TERM
/opt/ocpp-db/runtime/usr/bin/mariadb-dump --no-defaults \
    --socket=/run/ocpp-db/mysql.sock --single-transaction --quick --hex-blob \
    --routines --events ocpp_cpp > "$dump.partial"
mv "$dump.partial" "$dump"
gzip "$dump"
gzip -t "$dump.gz"
# Backups deliberately remain until an operator archives/removes them.
