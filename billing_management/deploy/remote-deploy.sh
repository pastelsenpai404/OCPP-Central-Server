#!/usr/bin/env bash
# Source + prebuilt public SvelteKit assets; never enable shell tracing.
set -euo pipefail
umask 027
id=${1:?release}; base=${2:?base domain}; mode=${3:?mode}; jobs=${4:?jobs}; hash=${5:?hash}
[[ $(id -u) == 0 && $id =~ ^[0-9]{8}T[0-9]{6}Z-[a-f0-9]{12}$ ]]
[[ $base =~ ^[a-z0-9]+([.-][a-z0-9]+)*\.[a-z]{2,}$ && ${#base} -le 180 ]]
[[ $mode == prepare || $mode == deploy ]]
[[ $jobs =~ ^[1-4]$ && $hash =~ ^[a-f0-9]{64}$ ]]
exec 9>/run/lock/billing-management-deploy.lock
flock -n 9 || { echo 'Another billing deployment is running.' >&2; exit 1; }
stage=/var/tmp/billing-deploy-$id
root=/opt/billing-management
release=$root/releases/$id
config=/etc/billing-management
site=/etc/nginx/sites-available/billing-management.conf
link=/etc/nginx/sites-enabled/billing-management.conf
printf '%s  %s\n' "$hash" "$stage/source.tar.gz" | sha256sum -c -
. /etc/os-release
[[ $ID == debian || $ID == ubuntu ]]
# Check DNS and port ownership before touching running services.
if [[ $mode == deploy ]]; then
    python3 - "$base" <<'PY'
import socket, sys
for label in ('ev.admin', 'ev.admin.api', 'ev.customer', 'ev.customer.api'):
    host = f'{label}.{sys.argv[1]}'
    addresses = {item[4][0] for item in socket.getaddrinfo(host, 443)}
    if addresses != {'104.248.96.73'}:
        raise SystemExit(f'DNS not ready: {host}')
PY
fi
for pair in management:5500 customer:5501; do
    area=${pair%:*}; port=${pair#*:}
    listener=$(ss -H -lntp "sport = :$port")
    if [[ -n $listener ]]; then
        pid=$(systemctl show "billing-$area.service" -p MainPID --value 2>/dev/null || echo 0)
        [[ $pid =~ ^[1-9][0-9]*$ ]] && grep -Fq "pid=$pid," <<< "$listener" || {
            echo "Billing port $port belongs to another service; no runtime changes made." >&2; exit 1;
        }
    fi
done
previous=$(readlink -f "$root/current" || true)
activated=0; nginx_changed=0; swap_created=0
declare -A was_active was_enabled
backup() { if [[ -f $1 ]]; then cp -a "$1" "$stage/$2.previous"; fi; }
backup "$site" nginx
for area in management customer; do
    backup "$config/$area.env" "$area-env"
    backup "/etc/systemd/system/billing-$area.service" "$area-unit"
    was_active[$area]=$(systemctl is-active "billing-$area.service" 2>/dev/null || true)
    was_enabled[$area]=$(systemctl is-enabled "billing-$area.service" 2>/dev/null || true)
done
finish() {
    code=$?; trap - EXIT; set +e
    if [[ $code != 0 && $activated == 1 ]]; then
        echo 'Billing deployment failed; restoring previous services and release.' >&2
        systemctl stop billing-management.service billing-customer.service
        if [[ -n $previous && -d $previous ]]; then
            ln -sfn "$previous" "$root/current.rollback"; mv -Tf "$root/current.rollback" "$root/current"
        else rm -f -- "$root/current"; fi
        for area in management customer; do
            for pair in "$area-env:$config/$area.env" "$area-unit:/etc/systemd/system/billing-$area.service"; do
                name=${pair%%:*}; target=${pair#*:}
                if [[ -f $stage/$name.previous ]]; then cp -a "$stage/$name.previous" "$target"; else rm -f -- "$target"; fi
            done
        done
        systemctl daemon-reload
        for area in management customer; do
            if [[ ${was_enabled[$area]} != enabled ]]; then systemctl disable "billing-$area.service" >/dev/null 2>&1; fi
            if [[ ${was_active[$area]} == active ]]; then systemctl start "billing-$area.service"; fi
        done
    fi
    if [[ $code != 0 && $nginx_changed == 1 ]]; then
        if [[ -f $stage/nginx.previous ]]; then cp -a "$stage/nginx.previous" "$site"; else rm -f -- "$site" "$link"; fi
        nginx -t && systemctl reload nginx
    fi
    if [[ $swap_created == 1 ]]; then
        if swapoff /var/lib/billing-management-build.swap; then rm -f /var/lib/billing-management-build.swap;
        else echo 'Build swap retained because swapoff failed; it is not in fstab.' >&2; fi
    fi
    exit "$code"
}
trap finish EXIT
trap 'exit 130' INT TERM
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-remove --no-install-recommends git cmake ninja-build g++ python3 curl nginx certbot
install -d -m 0755 "$root" "$root/releases" "$root/build-source" "$stage/source"
python3 - "$stage/source.tar.gz" "$stage/source" <<'PY'
import sys, tarfile
from pathlib import Path, PurePosixPath
with tarfile.open(sys.argv[1]) as archive:
    for item in archive.getmembers():
        path = PurePosixPath(item.name)
        if path.is_absolute() or '..' in path.parts or not (item.isfile() or item.isdir()):
            raise SystemExit('Unsafe archive member')
    archive.extractall(sys.argv[2])
for script in Path(sys.argv[2]).rglob('*.sh'):
    script.write_bytes(script.read_bytes().replace(b'\r\n', b'\n'))
PY
cp -a "$stage/source/." "$root/build-source/"
available=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
swap=$(awk '/SwapTotal:/ {print $2}' /proc/meminfo)
if [[ $available -lt 786432 && $swap -lt 1048576 ]]; then
    [[ ! -e /var/lib/billing-management-build.swap ]]
    fallocate -l 2G /var/lib/billing-management-build.swap
    chmod 0600 /var/lib/billing-management-build.swap
    mkswap /var/lib/billing-management-build.swap >/dev/null
    swapon /var/lib/billing-management-build.swap
    swap_created=1
fi
nice -n 10 cmake -S "$root/build-source" -B "$root/build-cache" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' \
    -DBILLING_BUILD_HTTP=ON -DCMAKE_INSTALL_PREFIX="$root/package"
nice -n 10 cmake --build "$root/build-cache" --parallel "$jobs"
ctest --test-dir "$root/build-cache" --output-on-failure
[[ ! -e $release ]]
cmake --install "$root/build-cache" --prefix "$release" --component Runtime
cp -a "$stage/source/web" "$release/web"
cp -a "$stage/source/deploy" "$release/deploy"
install -d -m 0755 "$release/licenses"
for dependency in billing_httplib billing_json; do
    find "$root/build-cache/_deps/$dependency-src" -maxdepth 1 -type f -iname '*license*' \
        -exec cp '{}' "$release/licenses/$dependency-license" \;
done
chmod -R a+rX "$release"
printf '%s\n' "$hash" > "$release/source.sha256"
for binary in billing_management_api billing_customer_api; do
    ldd "$release/bin/$binary" > "$release/$binary-libraries.txt"
    if grep -q 'not found' "$release/$binary-libraries.txt"; then exit 1; fi
done
for area in management customer; do
    getent passwd "billing-$area" >/dev/null || useradd --system --user-group --home-dir /nonexistent --shell /usr/sbin/nologin "billing-$area"
done
install -d -m 0700 "$config"
# An online SQLite backup preserves a consistent WAL snapshot before activation.
if [[ -f /var/lib/billing-management/backoffice.sqlite3 ]]; then
    install -d -m 0700 /var/backups/billing-management
    python3 - "$id" <<'PY'
import sqlite3, sys
source = sqlite3.connect('file:/var/lib/billing-management/backoffice.sqlite3?mode=ro', uri=True)
target = sqlite3.connect('/var/backups/billing-management/' + sys.argv[1] + '.sqlite3')
with target:
    source.backup(target)
target.close()
source.close()
PY
fi
activated=1
python3 "$release/deploy/configure.py" environment "$base" "$config"
for area in management customer; do
    sed "s/@AREA@/$area/g" "$release/deploy/billing-api.service.in" > "/etc/systemd/system/billing-$area.service"
    chmod 0644 "/etc/systemd/system/billing-$area.service"
done
ln -sfn "$release" "$root/current.next"; mv -Tf "$root/current.next" "$root/current"
systemctl daemon-reload
systemctl enable billing-management.service billing-customer.service
systemctl restart billing-management.service billing-customer.service
for pair in management:5500 customer:5501; do
    area=${pair%:*}; port=${pair#*:}; ready=0
    label=ev.admin.api; [[ $area == management ]] || label=ev.customer.api
    for attempt in {1..30}; do
        if curl --noproxy '*' -fsS --max-time 2 -H "Host: $label.$base" "http://127.0.0.1:$port/health/ready" >/dev/null 2>&1; then ready=1; break; fi
        sleep 1
    done
    [[ $ready == 1 ]] || { echo "Readiness failed: billing-$area" >&2; exit 1; }
done
python3 "$release/deploy/smoke.py" "$base" "$config"
install -d -m 0755 /var/www/billing-acme/.well-known/acme-challenge
nginx_changed=1
# Preserve existing HTTPS in prepare mode; regenerate CSP hashes for new HTML.
certificate=/etc/letsencrypt/live/billing-management-$base/fullchain.pem
if [[ $mode == prepare && -f $certificate && -f $stage/nginx.previous ]]; then
    python3 "$release/deploy/configure.py" nginx-tls "$base" "$release" > "$site"
else
    python3 "$release/deploy/configure.py" nginx-http "$base" "$release" > "$site"
fi
chmod 0644 "$site"
[[ -e $link ]] || ln -s "$site" "$link"
nginx -t; systemctl reload nginx
if [[ $mode == prepare ]]; then
    echo "PREPARED: two billing APIs passed checks. Create four A records pointing to 104.248.96.73."
    exit 0
fi
python3 "$release/deploy/certbot.py" certonly --webroot -w /var/www/billing-acme --non-interactive --agree-tos \
    --register-unsafely-without-email --cert-name "billing-management-$base" --keep-until-expiring \
    -d "ev.admin.$base" -d "ev.admin.api.$base" -d "ev.customer.$base" -d "ev.customer.api.$base"
python3 "$release/deploy/configure.py" nginx-tls "$base" "$release" > "$site"
chmod 0644 "$site"
nginx -t; systemctl reload nginx
install -d -m 0755 /etc/letsencrypt/renewal-hooks/deploy
printf '#!/bin/sh\nset -eu\n/usr/sbin/nginx -t\n/bin/systemctl reload nginx\n' > /etc/letsencrypt/renewal-hooks/deploy/billing-nginx
chmod 0755 /etc/letsencrypt/renewal-hooks/deploy/billing-nginx
systemctl enable --now certbot.timer
python3 "$release/deploy/smoke.py" "$base" "$config" --tls
for label in ev.admin ev.customer; do
    curl --noproxy '*' -fsS --max-time 15 --resolve "$label.$base:443:127.0.0.1" "https://$label.$base/" >/dev/null
    curl --noproxy '*' -fsS --max-time 15 --resolve "$label.$base:443:127.0.0.1" "https://$label.$base/wasm/billing.wasm" >/dev/null
done
echo "DEPLOYED: billing release $id (four HTTPS subdomains)."
