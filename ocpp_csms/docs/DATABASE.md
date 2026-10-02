# Database provisioned for the C++ server

Host: `104.248.96.73` (Debian 12). A dedicated MariaDB **10.11.18** instance runs as Unix user `ocppdb`, using Debian packages extracted into `/opt/ocpp-db/runtime`. No MariaDB packages were installed over the existing 10.3 service. The old service remains active on its original port.

| Setting | Value |
| --- | --- |
| Service | `ocpp-db.service`, enabled at boot |
| Database | `ocpp_cpp` |
| Application account | `ocpp_cpp_app` restricted to source `127.0.0.1` |
| Application privileges | SELECT, INSERT, UPDATE, DELETE on `ocpp_cpp.*`; no schema/admin privileges |
| Remote endpoint | `127.0.0.1:3307`, loopback only |
| Windows tunnel endpoint | `127.0.0.1:13307` |
| Data directory | `/var/lib/ocpp-db`, owned by `ocppdb`, mode 0700 |
| Admin socket | `/run/ocpp-db/mysql.sock`, Unix socket authentication |
| Config | `/etc/ocpp-db.cnf` |
| Runtime package directory | `/opt/ocpp-db/packages` |
| Connection limits | 24 total; 12 for the application account |
| Memory settings | 64 MiB InnoDB buffer pool; 256 MiB systemd limit |

The fresh schema contains 15 legacy table definitions plus 23 C++ runtime/protocol/sandbox tables (38 total). Historical Java migration records and application data were not imported. Initial settings use a 60-second heartbeat. `CP01` is provisioned with a random station secret. Random database password, API tokens and station secret are stored in ignored `config.local.json`, with Windows permissions limited to the current user, SYSTEM and Administrators. A server-local copy at `/etc/ocpp-cpp/config.local.json` uses port 3307 and has mode 0600. Never overwrite these files with `-InitConfig`.

## Run from Windows

In the first PowerShell console:

```powershell
cd 'C:\Users\usEr\Desktop\funny_project\OCPP Central Server\ocpp_csms'
.\run-db-tunnel.ps1
```

Enter the passphrase for `../material/nene_key_private.ppk` when PuTTY prompts. The script pins the existing deployment's SSH host fingerprint and binds the local forward only to loopback. Leave this console open. In a second console:

```powershell
cd 'C:\Users\usEr\Desktop\funny_project\OCPP Central Server\ocpp_csms'
.\run-server.ps1
```

The server listens on `http://127.0.0.1:5003`. Stop with Ctrl+C in each console. The passphrase is not saved in either launcher. The DB configuration is already filled in. The database setup does not itself deploy the application; use `deploy.ps1` and the [deployment guide](DEPLOYMENT.md) to install `ocpp-cpp.service`, Nginx and HTTPS on the prepared host.

## TLS

The new instance supports TLS 1.2/1.3. The application DB account requires SSL. Its server certificate has SANs for `localhost` and `127.0.0.1`, signed by a dedicated private CA. `db-ca.local.pem` supplies the CA to the Windows C++ connector; the launch script resolves that relative path beside the configuration. Certificate validation remains enabled even inside SSH.

Keys and certificates are in `/etc/ocpp-db/tls`. The CA private key is root-only; the server key is readable by the dedicated DB group. The server certificate has an 825-day validity and requires renewal before expiration; the CA has a 3650-day validity. Inspect the actual expiration with:

```sh
openssl x509 -in /etc/ocpp-db/tls/server.pem -noout -enddate
```

## Backups and maintenance

`ocpp-db-backup.timer` runs daily at 20:00 UTC with up to ten minutes of jitter and catches up after downtime. `backup-ocpp-db` uses a single-transaction dump, gzip compression and integrity verification. Root-only backups live in `/var/backups/ocpp-db`. An initial dump was restored into a new temporary database and verified to contain the initial 18 tables and `CP01`; that temporary database was then removed. Migrations 002 through 006 were applied later with verified backups, expanding the isolated database to 38 tables. A fresh backup of that final schema was subsequently restored into another new temporary database: all 38 tables, CP01 and the empty sandbox ledger were verified, then only that temporary database was dropped.

```sh
systemctl status ocpp-db ocpp-db-backup.timer
systemctl start ocpp-db-backup.service
journalctl -u ocpp-db --no-pager -n 50
/opt/ocpp-db/runtime/usr/bin/mariadb --no-defaults --socket=/run/ocpp-db/mysql.sock
```

Backups remain on this host until archived/removed by an operator. Off-host backup delivery is not configured. Copy them to an independently protected destination and monitor disk space.

Extracted packages are **not automatically upgraded by apt**. For a security update, take and verify a backup, download the matching Debian `mariadb-server`, `mariadb-server-core`, `mariadb-client` and `mariadb-client-core` packages into a new runtime directory, inspect dependencies and release notes, and replace the runtime only while `ocpp-db` is stopped. Apply required upgrade steps against this instance's socket and recheck readiness and restoreability. Leave the existing MariaDB service untouched. Increase capacity settings only after measuring the shared host's available memory; this host has about 1 GiB RAM and no swap.

## Verified

- Pinned SSH connection and loopback forwarding from Windows.
- Verified TLS connection using both PyMySQL and the actual C++ connector/server.
- Database-backed readiness, authenticated REST transaction reads, unauthenticated rejection.
- OCPP BootNotification, Heartbeat and unknown-tag rejection using a temporary station.
- Backup restore into a fresh temporary DB. Verification station/events were removed; the provisioned DB contains `CP01`, no transactions, and no test outbox rows.

These are database setup checks, not a pentest or verification of the incomplete Java-to-C++ migration. See `PARITY.md` and `VALIDATION.md` for application scope.
