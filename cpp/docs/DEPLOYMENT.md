# Deploy from Windows to 104.248.96.73

The launcher is `cpp/deploy.ps1`. Its default host is the existing Debian server, default hostname is `ocpp.barryofeverything.com`, and default application port is loopback-only 5003. It uses the pinned SSH host key and your existing PuTTY key through Pageant. It never saves the SSH passphrase or prints application credentials.

## First deployment

Install PuTTY (plink, pscp, Pageant) on Windows and keep `../material/nene_key_private.ppk` accessible. Windows `tar.exe` is also required. The server must retain its existing isolated `ocpp-db.service` and private `/etc/ocpp-cpp/config.local.json` configuration.

In DigitalOcean DNS for `barryofeverything.com`, create **A / ocpp / 104.248.96.73 / TTL 300**. Avoid a conflicting CNAME or AAAA record. Then run from the `cpp` directory:

```powershell
./deploy.ps1
```

Alternatively let the script create the DNS record. Enter a DigitalOcean token with DNS read/create permissions at the masked console prompt; do not paste it into source or shell arguments:

```powershell
./deploy.ps1 -CreateDns
```

If DNS is still propagating, rerun when it resolves directly to the server. A conflicting existing record is not overwritten. Use `-Domain ocpp.yourdomain.com` to select another domain managed in your DigitalOcean account.

You can build and test the service before DNS is ready:

```powershell
./deploy.ps1 -PrepareOnly
```

This installs the local service and an ACME HTTP virtual host that returns 503 for other paths. It does **not** enable public administration or issue a certificate. Rerun without `-PrepareOnly` after DNS is ready.

## What the script performs

1. Packages whitelisted sources, schemas, UI and deployment templates. Local configuration, keys, `.deps/` and Windows binaries are excluded. The upload is SHA-256 verified.
2. Installs Debian/Ubuntu build dependencies using apt with package removal forbidden. Existing shared libraries may be updated by apt. Builds pinned sources on Linux using one worker by default. The 1 GiB host uses an owned temporary 2 GiB swap file if needed; this is removed after the build when swapoff succeeds and is never added to fstab.
3. Checks additive runtime table presence, builds with warnings-as-errors and runs core/boundary checks. It does not import historical data or execute the old Java migrations.
4. Installs a versioned release under `/opt/ocpp-cpp/releases/`, switches `current`, and starts `ocpp-cpp.service` as the restricted `ocpp` Unix account. Existing server JSON secrets remain the source of configuration. Runtime settings use DB loopback port 3307 and two workers; the generated root-only `/etc/ocpp-cpp/runtime.env` configures the exact HTTPS origin.
5. Runs read-only readiness/auth/origin checks. Adds only its `ocpp-cpp.conf` Nginx virtual host, runs `nginx -t`, and reloads Nginx.
6. Obtains a dedicated Let's Encrypt certificate using webroot HTTP-01, redirects HTTP to HTTPS, enables Certbot renewal and a tested Nginx reload hook. Existing certificate/account settings are used; a first account can be registered without email.
7. Checks HTTPS readiness locally and from Windows, plus authenticated/unauthenticated WebSocket handshakes through the TLS gateway. Failed server activation restores the previous application symlink, environment, unit and virtual-host configuration. Package installs, successful additive schema changes and issued certificates are retained. A failed final Windows connectivity check reports the failure without undoing a server that passed its own checks.

The Nginx template forwards WebSocket Upgrade/Connection headers, uses 120-second upstream inactivity timeout, disables buffering and bounds bodies to 64 KiB. Application authentication and bounds remain enforced. HTTPS browser origins are configured explicitly; arbitrary `X-Forwarded-*` headers are not trusted for authorization. See [Nginx WebSocket documentation](https://nginx.org/en/docs/http/websocket.html), [Certbot webroot/renewal documentation](https://eff-certbot.readthedocs.io/en/stable/using.html), and [DigitalOcean DNS API](https://docs.digitalocean.com/products/networking/dns/reference/api/domain-records/).

The build cache uses a fixed install-prefix configuration; the runtime component is installed into the new release with `--prefix`. Changing a release ID therefore does not regenerate dependency headers and rebuild the network library. Releases include upstream license files, `runtime-libraries.txt` and a direct distribution-package version manifest. These are useful deployment records, not an exhaustive SBOM or vulnerability assessment. Linux uses Debian distribution libraries; Windows source-pin review does not establish their patch status.

## Database changes

The prepared database already has the current six additive migrations. Ordinary deployments only check tables. After reviewing a new additive migration, use:

```powershell
./deploy.ps1 -ApplyMigrations
```

This takes an integrity-checked backup of `ocpp_cpp` first and accepts only `CREATE TABLE IF NOT EXISTS` statements. Changes requiring ALTER/DROP or data transformations must have a separate reviewed migration plan. No automatic restore is performed after partial DDL execution.

## Access and operations

- UI: `https://ocpp.barryofeverything.com/`, with the existing reader/operator/admin API token in your private `config.local.json`.
- Charger JSON: `wss://ocpp.barryofeverything.com/ocpp/<station-id>` with its existing station Basic credentials and OCPP subprotocol. Legacy WebSocket/SOAP paths retain their existing routes.
- Service: `systemctl status ocpp-cpp`, logs: `journalctl -u ocpp-cpp -n 100`.
- Nginx: `/etc/nginx/sites-available/ocpp-cpp.conf`, logs under `/var/log/nginx/ocpp-cpp.*.log`.
- TLS: `certbot certificates`, `certbot renew --dry-run`, `systemctl status certbot.timer`.
- Archives, logs and previous releases are retained for diagnosis; do not redistribute private runtime files or whole cache directories.

`-PackageOnly` creates an archive without contacting the server. `-BuildJobs` accepts 1–4; keep 1 on this host. `-Port` changes the loopback application port and rendered proxy together; choose an unused port.

Public access requires DNS control and inbound ports 80/443 at both host and cloud firewall. The script does not change firewall policy or other virtual hosts. The service and isolated billing sandbox remain a partial migration; deployment is not evidence of an independent pentest or complete Java parity.
