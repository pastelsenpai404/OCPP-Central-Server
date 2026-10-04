#!/usr/bin/env bash
set -euo pipefail
umask 027
id=${1:?release}; base=${2:?domain}; mode=${3:?mode}; jobs=${4:?jobs}; hash=${5:?hash}
[[ $(id -u) == 0 && $id =~ ^[0-9]{8}T[0-9]{6}Z-[a-f0-9]{12}$ ]]
[[ $base =~ ^[a-z0-9]+([.-][a-z0-9]+)*\.[a-z]{2,}$ && ${#base} -le 180 ]]
[[ $mode == prepare || $mode == deploy ]]; [[ $jobs =~ ^[1-4]$ && $hash =~ ^[a-f0-9]{64}$ ]]
exec 9>/run/lock/ev-simulation-deploy.lock
flock -n 9 || { echo 'Another simulator deployment is running.' >&2; exit 1; }
stage=/var/tmp/ev-simulation-deploy-$id
root=/opt/ev-simulation
release=$root/releases/$id
config=/etc/ev-simulation
site=/etc/nginx/sites-available/ev-simulation.conf
link=/etc/nginx/sites-enabled/ev-simulation.conf
unit=/etc/systemd/system/ev-simulation.service
domain=ev.simulation.$base
printf '%s  %s\n' "$hash" "$stage/source.tar.gz" | sha256sum -c -
. /etc/os-release
[[ $ID == debian || $ID == ubuntu ]]
if [[ $mode == deploy ]]; then
  python3 - "$domain" <<'PY'
import socket,sys
if {i[4][0] for i in socket.getaddrinfo(sys.argv[1],443)}!={'104.248.96.73'}:raise SystemExit('DNS not ready')
PY
fi
listener=$(ss -H -lntp 'sport = :5600')
if [[ -n $listener ]]; then
  pid=$(systemctl show ev-simulation.service -p MainPID --value 2>/dev/null || echo 0)
  [[ $pid =~ ^[1-9][0-9]*$ ]] && grep -Fq "pid=$pid," <<< "$listener" || { echo 'Port 5600 belongs to another service.' >&2; exit 1; }
fi
previous=$(readlink -f "$root/current" || true)
active=$(systemctl is-active ev-simulation.service 2>/dev/null || true)
enabled=$(systemctl is-enabled ev-simulation.service 2>/dev/null || true)
activated=0; nginx_changed=0; swap_created=0
for pair in "$site:nginx" "$unit:unit" "$config/runtime.env:environment"; do
  path=${pair%:*}; name=${pair#*:}; if [[ -f $path ]]; then cp -a "$path" "$stage/$name.previous"; fi
done
finish() {
  code=$?; trap - EXIT; set +e
  if [[ $code != 0 && $activated == 1 ]]; then
    echo 'Simulator deployment failed; restoring previous runtime.' >&2
    systemctl stop ev-simulation.service
    if [[ -n $previous && -d $previous ]]; then ln -sfn "$previous" "$root/current.rollback"; mv -Tf "$root/current.rollback" "$root/current"; else rm -f -- "$root/current"; fi
    for pair in "$unit:unit" "$config/runtime.env:environment"; do
      path=${pair%:*}; name=${pair#*:}; if [[ -f $stage/$name.previous ]]; then cp -a "$stage/$name.previous" "$path"; else rm -f -- "$path"; fi
    done
    systemctl daemon-reload
    if [[ $enabled != enabled ]]; then systemctl disable ev-simulation.service >/dev/null 2>&1; fi
    if [[ $active == active ]]; then systemctl start ev-simulation.service; fi
  fi
  if [[ $code != 0 && $nginx_changed == 1 ]]; then
    if [[ -f $stage/nginx.previous ]]; then cp -a "$stage/nginx.previous" "$site"; else rm -f -- "$site" "$link"; fi
    nginx -t && systemctl reload nginx
  fi
  if [[ $swap_created == 1 ]]; then
    if swapoff /var/lib/ev-simulation-build.swap; then rm -f /var/lib/ev-simulation-build.swap; else echo 'Build swap retained; not added to fstab.' >&2; fi
  fi
  exit "$code"
}
trap finish EXIT
trap 'exit 130' INT TERM
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-remove --no-install-recommends git cmake ninja-build g++ libssl-dev python3 curl nginx certbot
install -d -m 0755 "$root" "$root/releases" "$root/build-source" "$stage/source"
python3 - "$stage/source.tar.gz" "$stage/source" <<'PY'
import sys,tarfile
from pathlib import Path,PurePosixPath
with tarfile.open(sys.argv[1]) as archive:
    for member in archive.getmembers():
        p=PurePosixPath(member.name)
        if p.is_absolute() or '..' in p.parts or not(member.isfile() or member.isdir()):raise SystemExit('Unsafe archive')
    archive.extractall(sys.argv[2])
for path in Path(sys.argv[2]).rglob('*.sh'):path.write_bytes(path.read_bytes().replace(b'\r\n',b'\n'))
PY
cp -a "$stage/source/." "$root/build-source/"
available=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
swap=$(awk '/SwapTotal:/ {print $2}' /proc/meminfo)
if [[ $available -lt 786432 && $swap -lt 1048576 ]]; then
  [[ ! -e /var/lib/ev-simulation-build.swap ]]
  fallocate -l 2G /var/lib/ev-simulation-build.swap; chmod 0600 /var/lib/ev-simulation-build.swap
  mkswap /var/lib/ev-simulation-build.swap >/dev/null; swapon /var/lib/ev-simulation-build.swap; swap_created=1
fi
nice -n 10 cmake -S "$root/build-source" -B "$root/build-cache" -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG'
nice -n 10 cmake --build "$root/build-cache" --parallel "$jobs"
ctest --test-dir "$root/build-cache" --output-on-failure
[[ ! -e $release ]]
cmake --install "$root/build-cache" --prefix "$release" --component Runtime
cp -a "$stage/source/web" "$release/web"
cp -a "$stage/source/deploy" "$release/deploy"
install -d -m 0755 "$release/licenses"
for dep in sim_http sim_json; do find "$root/build-cache/_deps/$dep-src" -maxdepth 1 -type f -iname '*license*' -exec cp '{}' "$release/licenses/$dep-license" \;; done
chmod -R a+rX "$release"
printf '%s\n' "$hash" > "$release/source.sha256"
ldd "$release/bin/ev_charger_server" > "$release/libraries.txt"
if grep -q 'not found' "$release/libraries.txt"; then exit 1; fi
getent passwd ev-simulation >/dev/null || useradd --system --user-group --home-dir /nonexistent --shell /usr/sbin/nologin ev-simulation
install -d -m 0700 "$config"
activated=1
python3 "$release/deploy/configure.py" environment "$base" "$config"
install -m 0644 "$release/deploy/ev-simulation.service" "$unit"
ln -sfn "$release" "$root/current.next"; mv -Tf "$root/current.next" "$root/current"
systemctl daemon-reload; systemctl enable ev-simulation.service; systemctl restart ev-simulation.service
ready=0
for attempt in {1..30}; do
  if curl --noproxy '*' -fsS --max-time 2 -H "Host: $domain" http://127.0.0.1:5600/health/ready >/dev/null 2>&1; then ready=1; break; fi
  sleep 1
done
[[ $ready == 1 ]] || { echo 'Simulator readiness failed.' >&2; exit 1; }
python3 "$release/deploy/smoke.py" "$base" "$config"
install -d -m 0755 /var/www/ev-simulation-acme/.well-known/acme-challenge
nginx_changed=1
if [[ $mode == prepare && -f /etc/letsencrypt/live/ev-simulation-$base/fullchain.pem && -f $stage/nginx.previous ]]; then
  python3 "$release/deploy/configure.py" nginx-tls "$base" "$release" > "$site"
else python3 "$release/deploy/configure.py" nginx-http "$base" "$release" > "$site"; fi
chmod 0644 "$site"; [[ -e $link ]] || ln -s "$site" "$link"
nginx -t; systemctl reload nginx
if [[ $mode == prepare ]]; then echo "PREPARED: simulator ready in mock mode. Create A ev.simulation -> 104.248.96.73, then deploy HTTPS."; exit 0; fi
python3 "$release/deploy/certbot.py" certonly --webroot -w /var/www/ev-simulation-acme --non-interactive --agree-tos --register-unsafely-without-email \
  --cert-name "ev-simulation-$base" --keep-until-expiring -d "$domain"
python3 "$release/deploy/configure.py" nginx-tls "$base" "$release" > "$site"
nginx -t; systemctl reload nginx
install -d -m 0755 /etc/letsencrypt/renewal-hooks/deploy
printf '#!/bin/sh\nset -eu\n/usr/sbin/nginx -t\n/bin/systemctl reload nginx\n' > /etc/letsencrypt/renewal-hooks/deploy/ev-simulation-nginx
chmod 0755 /etc/letsencrypt/renewal-hooks/deploy/ev-simulation-nginx
systemctl enable --now certbot.timer
python3 "$release/deploy/smoke.py" "$base" "$config" --tls
curl --noproxy '*' -fsS --max-time 15 --resolve "$domain:443:127.0.0.1" "https://$domain/" >/dev/null
curl --noproxy '*' -fsS --max-time 15 --resolve "$domain:443:127.0.0.1" "https://$domain/wasm/physics.wasm" >/dev/null
echo "DEPLOYED: https://$domain/ release $id"
