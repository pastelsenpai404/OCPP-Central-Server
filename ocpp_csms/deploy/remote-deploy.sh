#!/usr/bin/env bash
# Called by deploy.ps1. Never enable shell tracing: configuration contains secrets.
set -euo pipefail
umask 027
release_id=${1:?release id}; domain=${2:?domain}; mode=${3:?mode}
port=${4:?port}; jobs=${5:?jobs}; apply_migrations=${6:?migration mode}
expected_hash=${7:?archive SHA256}
[[ $(id -u) == 0 ]] || { echo 'Run through the authorized root SSH account.' >&2; exit 1; }
[[ $release_id =~ ^[0-9]{8}T[0-9]{6}Z-[a-f0-9]{12}$ ]]
[[ $domain =~ ^[a-z0-9]+([.-][a-z0-9]+)*$ && $domain == ocpp.* ]]
[[ $mode == prepare || $mode == deploy ]]
[[ $port =~ ^[0-9]+$ && $port -ge 1024 && $port -le 65535 ]]
[[ $jobs =~ ^[1-4]$ && $apply_migrations =~ ^[01]$ && $expected_hash =~ ^[a-f0-9]{64}$ ]]
exec 9>/run/lock/ocpp-cpp-deploy.lock
flock -n 9 || { echo 'Another OCPP deploy is running.' >&2; exit 1; }
stage=/var/tmp/ocpp-deploy-$release_id
root=/opt/ocpp-cpp
release=$root/releases/$release_id
archive=$stage/source.tar.gz
printf '%s  %s\n' "$expected_hash" "$archive" | sha256sum -c -
[[ -f /etc/ocpp-cpp/config.local.json && -S /run/ocpp-db/mysql.sock ]]
listener=$(ss -H -lntp "sport = :$port")
if [[ -n $listener ]]; then
    app_pid=$(systemctl show ocpp-cpp.service -p MainPID --value 2>/dev/null || echo 0)
    if [[ ! $app_pid =~ ^[1-9][0-9]*$ ]] || ! grep -Fq "pid=$app_pid," <<< "$listener"; then
        echo 'Requested application port belongs to another service; no runtime changes made.' >&2
        exit 1
    fi
fi
. /etc/os-release
[[ $ID == debian || $ID == ubuntu ]] || { echo 'Debian/Ubuntu host required.' >&2; exit 1; }

previous=$(readlink -f "$root/current" || true)
activated=0; nginx_changed=0; created_swap=0
site=/etc/nginx/sites-available/ocpp-cpp.conf
link=/etc/nginx/sites-enabled/ocpp-cpp.conf
unit=/etc/systemd/system/ocpp-cpp.service
backup_file() { if [[ -f $1 ]]; then cp -a -- "$1" "$stage/$2.previous"; fi; }
backup_file "$site" nginx
backup_file "$unit" unit
backup_file /etc/ocpp-cpp/runtime.env environment
was_enabled=$(systemctl is-enabled ocpp-cpp.service 2>/dev/null || true)
was_active=$(systemctl is-active ocpp-cpp.service 2>/dev/null || true)
finish() {
    code=$?
    trap - EXIT
    set +e
    if [[ $code != 0 && $activated == 1 ]]; then
        echo 'Deployment failed; restoring the previous runtime.' >&2
        systemctl stop ocpp-cpp.service
        if [[ -n $previous && -d $previous ]]; then
            ln -sfn "$previous" "$root/current.rollback"; mv -Tf "$root/current.rollback" "$root/current"
        else
            rm -f -- "$root/current"
        fi
        for pair in 'environment:/etc/ocpp-cpp/runtime.env' "unit:$unit"; do
            name=${pair%%:*}; target=${pair#*:}
            if [[ -f $stage/$name.previous ]]; then cp -a "$stage/$name.previous" "$target"; else rm -f -- "$target"; fi
        done
        systemctl daemon-reload
        if [[ $was_enabled != enabled ]]; then systemctl disable ocpp-cpp.service >/dev/null 2>&1; fi
        if [[ $was_active == active ]]; then systemctl start ocpp-cpp.service; fi
    fi
    if [[ $code != 0 && $nginx_changed == 1 ]]; then
        if [[ -f $stage/nginx.previous ]]; then cp -a "$stage/nginx.previous" "$site"; else rm -f -- "$link" "$site"; fi
        nginx -t && systemctl reload nginx
    fi
    if [[ $created_swap == 1 ]]; then
        if swapoff /var/lib/ocpp-cpp-build.swap; then rm -f /var/lib/ocpp-cpp-build.swap;
        else echo 'Build swap is still in use; retained /var/lib/ocpp-cpp-build.swap (not in fstab).' >&2; fi
    fi
    exit "$code"
}
trap finish EXIT
trap 'exit 130' INT TERM

echo 'Installing Linux build dependencies (package removal is forbidden)...'
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-remove --no-install-recommends git cmake ninja-build g++ \
    libjsoncpp-dev zlib1g-dev libmariadb-dev libssl-dev libcurl4-openssl-dev uuid-dev \
    python3 curl nginx certbot
install -d -m 0755 "$root" "$root/releases" "$root/build-source" "$root/build-cache" "$stage/source"
python3 - "$archive" "$stage/source" <<'PY'
import sys, tarfile
from pathlib import PurePosixPath
with tarfile.open(sys.argv[1]) as archive:
    for item in archive.getmembers():
        path = PurePosixPath(item.name)
        if path.is_absolute() or '..' in path.parts or not (item.isfile() or item.isdir()):
            raise SystemExit('Unsafe archive member')
    archive.extractall(sys.argv[2])
from pathlib import Path
for script in Path(sys.argv[2]).rglob('*.sh'):
    script.write_bytes(script.read_bytes().replace(b'\r\n', b'\n'))
PY
source_root=$root/build-source
# Copy only source: runtime secrets and generated files are not in the archive.
cp -a "$stage/source/." "$source_root/"
python3 "$source_root/scripts/check-architecture.py"
if [[ $apply_migrations == 1 ]]; then
    bash "$source_root/deploy/backup-ocpp-db.sh"
    python3 "$source_root/deploy/schema.py" "$source_root" apply
else
    python3 "$source_root/deploy/schema.py" "$source_root" check
fi

# One-job builds on the existing 1 GiB host need temporary swap for C++ templates.
available=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
swap_total=$(awk '/SwapTotal:/ {print $2}' /proc/meminfo)
if [[ $available -lt 786432 && $swap_total -lt 1048576 ]]; then
    [[ ! -e /var/lib/ocpp-cpp-build.swap ]] || { echo 'Existing build swap needs operator inspection.' >&2; exit 1; }
    fallocate -l 2G /var/lib/ocpp-cpp-build.swap
    chmod 0600 /var/lib/ocpp-cpp-build.swap
    mkswap /var/lib/ocpp-cpp-build.swap >/dev/null
    swapon /var/lib/ocpp-cpp-build.swap
    created_swap=1
fi
echo 'Building pinned sources and running Linux core checks...'
nice -n 10 cmake -S "$source_root" -B "$root/build-cache" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' \
    -DCMAKE_INSTALL_PREFIX="$root/package"
nice -n 10 cmake --build "$root/build-cache" --parallel "$jobs"
ctest --test-dir "$root/build-cache" --output-on-failure
[[ ! -e $release ]]
cmake --install "$root/build-cache" --prefix "$release" --component Runtime
install -d -m 0755 "$release/deploy" "$release/migrations"
cp -a "$source_root/deploy/." "$release/deploy/"
cp -a "$source_root/migrations/." "$release/migrations/"
# Release files contain code/public assets only; the unprivileged service must traverse them.
chmod -R a+rX "$release"
printf '%s\n' "$expected_hash" > "$release/source.sha256"
dpkg-query -W -f='${binary:Package}\t${Version}\n' libmariadb3 libssl3 libcurl4 zlib1g libjsoncpp25 libuuid1 > "$release/runtime-packages.txt"
install -d -m 0755 "$release/share/ocpp/licenses"
for dependency in json schema_validator drogon pugixml; do
    while IFS= read -r -d '' license; do
        cp "$license" "$release/share/ocpp/licenses/$dependency-$(basename "$license")"
    done < <(find "$root/build-cache/_deps/$dependency-src" -maxdepth 1 -type f \( -iname 'license*' -o -iname 'copying*' \) -print0)
done
if [[ -f $root/build-cache/_deps/drogon-src/trantor/License ]]; then
    cp "$root/build-cache/_deps/drogon-src/trantor/License" "$release/share/ocpp/licenses/trantor-License"
fi
ldd "$release/bin/ocpp_server" | tee "$release/runtime-libraries.txt"
if grep -q 'not found' "$release/runtime-libraries.txt"; then exit 1; fi
getent passwd ocpp >/dev/null || useradd --system --user-group --home-dir /nonexistent --shell /usr/sbin/nologin ocpp
install -d -m 0755 /etc/ocpp-cpp
activated=1
python3 "$release/deploy/runtime-env.py" /etc/ocpp-cpp/config.local.json /etc/ocpp-cpp/runtime.env "$domain" "$port"
install -m 0644 "$release/deploy/ocpp-cpp.service" "$unit"
ln -sfn "$release" "$root/current.next"; mv -Tf "$root/current.next" "$root/current"
systemctl daemon-reload
systemctl enable ocpp-cpp.service
systemctl restart ocpp-cpp.service
ready=0
for attempt in {1..60}; do
    if curl --noproxy '*' -fsS --max-time 2 "http://127.0.0.1:$port/health/ready" >/dev/null 2>&1; then ready=1; break; fi
    sleep 1
done
[[ $ready == 1 ]] || { echo 'Service readiness failed; inspect journalctl -u ocpp-cpp.' >&2; exit 1; }
python3 "$release/deploy/smoke.py" "$domain" "$port" /etc/ocpp-cpp/config.local.json
install -d -m 0755 /var/www/ocpp-acme/.well-known/acme-challenge
if [[ ! -e $site ]]; then
    nginx_changed=1
    sed "s/@DOMAIN@/$domain/g" "$release/deploy/nginx-http.conf.in" > "$site"
    chmod 0644 "$site"; ln -s "$site" "$link"
    nginx -t; systemctl reload nginx
fi
if [[ $mode == prepare ]]; then
    echo "PREPARED: Linux service is ready; create DNS A $domain -> 104.248.96.73, then rerun without -PrepareOnly."
    exit 0
fi
python3 - "$domain" <<'PY'
import socket, sys
addresses = {item[4][0] for item in socket.getaddrinfo(sys.argv[1], 80, socket.AF_INET)}
if addresses != {'104.248.96.73'}:
    raise SystemExit('DNS must resolve directly to 104.248.96.73 before HTTPS activation')
PY
python3 "$release/deploy/certbot.py" certonly --webroot --webroot-path /var/www/ocpp-acme --non-interactive --agree-tos \
    --register-unsafely-without-email --cert-name "$domain" -d "$domain" --keep-until-expiring
nginx_changed=1
sed -e "s/@DOMAIN@/$domain/g" -e "s/@PORT@/$port/g" "$release/deploy/nginx.conf.in" > "$site"
chmod 0644 "$site"
[[ -e $link ]] || ln -s "$site" "$link"
nginx -t; systemctl reload nginx
install -d -m 0755 /etc/letsencrypt/renewal-hooks/deploy
printf '#!/bin/sh\nset -eu\n/usr/sbin/nginx -t\n/bin/systemctl reload nginx\n' > /etc/letsencrypt/renewal-hooks/deploy/ocpp-cpp-nginx
chmod 0755 /etc/letsencrypt/renewal-hooks/deploy/ocpp-cpp-nginx
systemctl enable --now certbot.timer
curl --noproxy '*' -fsS --max-time 20 --resolve "$domain:443:127.0.0.1" "https://$domain/health/ready" >/dev/null
curl --noproxy '*' -fsS --max-time 20 --resolve "$domain:443:127.0.0.1" "https://$domain/" >/dev/null
python3 "$release/deploy/smoke.py" "$domain" 443 /etc/ocpp-cpp/config.local.json --tls
echo "DEPLOYED: https://$domain/ (release $release_id)"
