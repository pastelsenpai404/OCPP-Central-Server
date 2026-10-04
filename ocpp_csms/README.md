# C++ OCPP migration

**Status: partial migration, not a production replacement for the Java server.**

This project builds a C++20 service for OCPP 1.2/1.5/1.6 JSON and SOAP, plus OCPP 2.0.1 JSON, for Windows and Linux. It preserves the existing MariaDB table names and writes additive runtime tables. The Java application and its configuration are unchanged. Legacy billing workflows and the JSP administration application remain to be migrated; billing currently has an isolated settlement simulator. See [the parity record](docs/PARITY.md) for remaining release work.

## Administration UI

Open `http://127.0.0.1:5003/` after `run-local.ps1` reports READY. The Control room UI provides a dashboard, station/transaction tables, schema-based OCPP commands and results, tags/tokens, reports/security/audit, and billing sandbox. Sign in with `OCPP_ADMIN_TOKEN` (or the appropriate operator/reader token) from the private `config.local.json`. The token remains in browser memory; reload/sign-out clears it. All writes require review and API-side authorization. See [UI operations and limits](docs/ADMIN-UI.md).

## Implemented

- Fourteen incoming OCPP 1.6 actions, including security notifications and verified CSR requests awaiting issuer review. OCPP 1.2/1.5 conversions preserve their meter formats, boot heartbeat fields and local-list spelling.
- OCPP 2.0.1: all 64 action schema pairs and direction handling, 25 station-initiated actions and 40 CSMS commands (DataTransfer overlaps); durable transaction events, sequence/replay conflict checks, late offline events, EVSE status, report chunks, device variables, local tokens/lists, reservations and profiles. Certificate-dependent positive paths require trusted providers; they currently fail closed.
- SOAP 1.2 envelopes for OCPP 1.2/1.5/1.6, typed namespace-checked bodies and WS-Addressing, authenticated version routes and the legacy namespace router. SOAP failures return Sender/Receiver fault envelopes, with validated message correlation when available. Outgoing SOAP uses explicitly configured device endpoints, bounded HTTP responses, verified HTTPS, and no redirects. Private literal IPv4 HTTP origins require explicit configuration.
- Request/response schema validation, strict envelopes, duplicate-member rejection, nesting/size/sample bounds, UTC conversion, integer range checks.
- Per-station random Basic credentials, reader/operator/admin bearer credentials, registration checks, transaction ownership checks, same-origin authenticated browser administration; browser origins remain denied on charger transports.
- Prepared SQL statements, bounded connection pool, explicit TLS verification for remote DB connections, atomic transaction/replay/outbox writes.
- Persistent request-ID replay for mutations and semantic start/stop deduplication, with station-row locking to serialize database changes. Authorization, boot and heartbeat responses always reflect current state rather than a cached decision/time.
- Bounded FIFO worker lanes, per-session in-flight limits, rate limiting, correlated outbound commands, disconnect/error/30-second timeout handling.
- Twenty-six OCPP 1.6 outbound actions, including firmware/diagnostics and security commands. Transfer URLs require an explicit HTTPS origin allowlist. Reservations, profiles and local-list state update only after validated device replies.
- Durable command tasks, audit outcomes and restart recovery to `Uncertain`; ambiguous commands are not automatically resent. Variable replies must match their requests. Stored network-profile requests are redacted from task reads.
- Batch prepared inserts for up to 512 bounded meter samples/rows. Sandbox billing uses integer milliWh and minor currency units, explicit tariffs, parking grace intervals and an idempotent simulation ledger; it executes no payment.
- Embedded responsive administration UI, role-aware navigation, schema-based command forms/advanced JSON, reviewed writes, current-page search/export and live-session metadata. Paginated station/tag/token/transaction/reservation/status/audit reads; authenticated provisioning and access updates; JSON counters and health endpoints.
- A pinned Drogon transport patch for aggregate fragmented-message limits, masked client frames, interleaved control frames, and a 1 MiB send-buffer high-water close.

## Code organization

See [architecture and module boundaries](docs/ARCHITECTURE.md) and [team development rules](CONTRIBUTING.md). Source, public headers, build targets and tests are grouped by responsibility. Run `python scripts/check-architecture.py` before review; CTest also runs this check when Python is available.

## Deploy to the prepared Linux host

Run `./deploy.ps1` for `ocpp.barryofeverything.com`, or `./deploy.ps1 -CreateDns` to enter a DigitalOcean DNS token at runtime. The script builds Linux sources, installs a restricted systemd service, configures Nginx/WebSocket proxying and HTTPS renewal, and verifies readiness. DNS must point to `104.248.96.73`; `-PrepareOnly` builds/tests the server while waiting for DNS. See [deployment instructions and recovery](docs/DEPLOYMENT.md).

## Build on Windows

Requires Git, Visual Studio 2022 C++ Build Tools with its CMake component, and internet access for pinned sources. Dependencies install under `.deps/`; no system compiler or database service is installed.

```powershell
./scripts/build-windows.ps1
```

Outputs: `build/Release/ocpp_server.exe`, `ocpp_tests.exe`, `ocpp_benchmark.exe`, `libmariadb.dll` and `z.dll`. Keep these two dependency DLLs with the executables. The script runs core tests. Package only those artifacts and required notices; a reused build directory can contain obsolete DLLs from earlier dependency versions.

### Run on Windows

For a local server using the database already provisioned on `104.248.96.73`, run this single launcher (from any working directory):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./run-local.ps1
```

It checks the prepared configuration, opens a pinned SSH tunnel, prompts for the key passphrase in the same console, starts the local C++ server and checks DB-backed readiness. Keep the console open; Ctrl+C closes its server and tunnel. The passphrase is not stored or passed as a command-line argument. The server listens on loopback, so this mode is for clients on this computer. PuTTY/plink is required; `config.local.json`, the DB CA and the private key under `material` are already prepared. Use `-Build` to rebuild first, or `-ConfigPath`/`-KeyPath` for another prepared configuration/key.

The separate `run-db-tunnel.ps1` and `run-server.ps1` launchers remain available. See [database setup and maintenance](docs/DATABASE.md). The initialization steps below are for a different/new configuration.

Versioned templates: [config.local.json.example](config.local.json.example) lists all supported settings with dummy credentials; [db-ca.local.pem.example](db-ca.local.pem.example) explains how to obtain the database CA. Neither template contains working credentials or a real CA certificate. Build outputs, dependencies, caches and logs are generated locally and do not need templates. No templates are provided for `material/`.

For a new checkout, use `-InitConfig` below to generate fresh API tokens and station credentials, then fill in the database settings using the example as a reference. Obtain the real CA separately and save it as `db-ca.local.pem`; set `OCPP_DB_CA` to that filename (or leave it empty for a local database that does not require TLS). The SSH tunnel launcher uses local port `13307` by default, while direct database connections use their configured database port. Set `OCPP_PUBLIC_ORIGIN` to an exact HTTPS origin when running behind the production proxy; leave it empty for local testing. Do not overwrite an existing private configuration or commit the filled-in files.

```powershell
./run-server.ps1 -InitConfig
notepad ./config.local.json
./run-server.ps1 -CheckConfig
./run-server.ps1
```

Initialization creates `config.local.json` with random API tokens and a station secret for `CP01` (override with `-StationId`). It refuses to overwrite an existing file. Fill in database name, user and password; prepare the database as described below. Keep this ignored file private because it contains credentials. The launcher checks configuration, executable, DLLs and schemas, then runs in the current console; stop with Ctrl+C. `-CheckConfig` does not connect to the database. Use `-Build` to build before launching, `-Configuration Debug` for a debug build, or `-ConfigPath C:/path/config.json` for another configuration. Relative CA paths resolve beside the configuration file. Environment settings apply only to the current process and are restored when the server exits. The script works from any working directory.

## Build on Linux

For Ubuntu 24.04 or a compatible environment:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake git ninja-build libjsoncpp-dev zlib1g-dev libmariadb-dev libssl-dev libcurl4-openssl-dev uuid-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Sanitizer build: configure a separate directory with `-DOCPP_SANITIZERS=ON` and `-DCMAKE_BUILD_TYPE=RelWithDebInfo`. The included GitHub workflow prepares Windows and Linux checks; it has not been run in this workspace. Linux execution has **not** been verified locally.

## Database and configuration

First restore a backup of the existing schema into an isolated database. Apply all six `migrations/*.sql` files in filename order explicitly to that database. The program checks its required tables at startup and readiness; it never auto-migrates or imports the legacy database. Tests use checked-in legacy schema definitions containing no historical data. The dedicated remote C++ database has these migrations applied; validate any other deployment schema separately.

Set these environment variables through your service/secret manager:

| Variable | Meaning |
| --- | --- |
| `OCPP_DB_NAME`, `OCPP_DB_USER`, `OCPP_DB_PASSWORD` | Database credentials; no defaults |
| `OCPP_DB_HOST`, `OCPP_DB_PORT` | Defaults `127.0.0.1`, `3306` |
| `OCPP_DB_CA` | Trusted CA PEM path, required for remote databases; verified TLS enforced |
| `OCPP_STATION_SECRETS` | JSON object mapping provisioned station IDs to unique random secrets |
| `OCPP_READ_TOKEN`, `OCPP_OPERATOR_TOKEN`, `OCPP_ADMIN_TOKEN` | Three distinct random API credentials |
| `OCPP_PORT`, `OCPP_WORKERS` | Defaults `5003`, `4`; workers limited to 32 |
| `OCPP_TRANSFER_ORIGINS` | JSON array of exact lowercase HTTPS origins for firmware/log/diagnostics locations; default empty |
| `OCPP_SOAP_ORIGINS` | JSON array of explicit device origins; HTTPS or literal private IPv4 HTTP; default empty |
| `OCPP_SOAP_ENDPOINTS` | JSON object `{station:{"version":"1.5","url":"https://device.example/ocpp"}}`; default empty |

Each station secret and API token must be **32 cryptographically random bytes encoded as 64 lowercase hex characters**. Generate with a secret manager or Python `secrets.token_hex(32)`; do not use repeated characters or human passwords. The syntax check cannot measure entropy. Station IDs allow ASCII letters, digits, `_`, `-`, up to 64 characters. Configuration is read at startup; rotating credentials currently requires a restart.

Run with the schema directory as its sole argument:

```powershell
./build/Release/ocpp_server.exe ./schemas
```

The listener binds **only to loopback**. Charger endpoints are `/ocpp/{station}`, `/steve/websocket/CentralSystemService/{station}`, and `/develop/websocket/CentralSystemService/{station}`. Clients must select exactly one of `ocpp1.2`, `ocpp1.5`, `ocpp1.6`, `ocpp2.0.1` and send `Authorization: Basic base64(station:secret)`. SOAP routes are `/develop/services/CentralSystemServiceOCPP12`, `15`, `16` or the namespace router `/develop/services/CentralSystemService`; `/services` and `/steve/services` aliases also exist. SOAP device authentication uses the configured station ID/secret. HTTP device addresses must come from configuration; WS-Addressing routing headers never configure them.

Use a separately operated TLS gateway for public WSS. `deploy/Caddyfile` is an untested starter configuration for both platforms that exposes charger routes only. Keep administrative REST access behind a private gateway. Local Basic and bearer credentials traverse loopback in plaintext; do not forward the backend port publicly. Configure gateway connection/rate/time limits; the application's peer IP is the loopback proxy address. The sample and systemd unit have not been deployed or validated here.

## REST interface

All REST endpoints require `Authorization: Bearer <token>`, except `/health/live` and `/health/ready`. No legacy GET mutations or login/session flows are provided. These are new contracts, not drop-in equivalents to the old `/dev` and `/api/v1` DTOs.

| Method/path | Role | Payload/response |
| --- | --- | --- |
| GET `/api/v1/{resource}?offset=0` | reader | At most 100 rows; chargepoints, transactions, ocppTags, reservations, connectorStatus |
| GET `/api/v1/audit` | admin | Command/admin action records |
| GET `/api/v1/metrics` | reader | frames, errors, overloads, active sessions |
| POST `/api/v1/chargepoints` | admin | `{"chargeBoxId":"CP01"}`; credentials must already be configured |
| POST `/api/v1/admin/chargepoints/register` | admin | Same ID body; idempotent registry-only creation for management integration. Returns `registryOnly` and `credentialsConfigured`; does not bypass station authentication or provision credentials. |
| POST `/api/v1/ocppTags` | admin | `{"idTag":"TAG","maxActiveTransactions":1}`; optional parentIdTag, expiryDate |
| POST `/api/v1/chargepoints/{station}/commands/{action}` | operator | OCPP request payload; waits asynchronously up to 30 seconds for validated result |
| GET `/api/v1/tasks/{commandId}` | operator | Durable task detail; network-profile request secrets are redacted |
| GET `/api/v1/chargepoints/{station}/state` | reader | Confirmed 1.6 state, profiles and local list |
| POST `/api/v1/idTokens` | admin | 2.0.1 `idToken` and `idTokenInfo` |
| POST `/api/v1/billing/sandbox/settle` | admin | Simulated settlement and idempotent case ID; see BILLING-SANDBOX.md |

SQL result fields currently use database column names and string/null values. This differs from legacy DTOs. Audit records identify actions/stations/command IDs, not individual administrators; production identity integration and outcome auditing remain outstanding. Like the customized Java code, tag authorization does not enforce positive `maxActiveTransactions` counts; zero blocks a tag, negative permits unlimited. Unknown offline transaction tags are recorded blocked to avoid losing transaction records.

## Tests and measured limits

Core tests cover malformed input, schemas, timestamps, access roles, rate limits, worker capacity and ordering. Integration tests run against a newly named temporary database and clean up only that database. The Windows helper starts a separate MariaDB process on an ephemeral loopback port, with a new data directory and a random root password; it does not register a Windows service or touch the original databases.

```powershell
python -m pip install websockets==15.0.1 PyMySQL==1.1.2
./scripts/test-windows-integration.ps1 -MariaDbDirectory C:/path/to/portable/mariadb
./scripts/test-windows-integration.ps1 -MariaDbDirectory C:/path/to/portable/mariadb -LoadStations 100
./build/Release/ocpp_benchmark.exe ./schemas
```

The integration fixture imports **schema definitions only** from the sibling reference dumps. Those fixtures are not bundled into a deployed installation. On Linux, run `tests/integration/integration.py --server build/ocpp_server --db-port <isolated-local-db-port>` with `OCPP_TEST_DB_PASSWORD` set for that local test server.

See `docs/VALIDATION.md` for actual results. The microbenchmark excludes SQL, network, TLS, billing, memory/RSS and concurrency. No throughput improvement over Java has been established. `tests/fuzz/message_fuzz.cpp` is a libFuzzer entry point, not evidence of a completed fuzz campaign.

## Work required before replacement

See `docs/PARITY.md` and the complete Java inventory. Outstanding work includes OCPP 1.2/1.5 JSON and SOAP, complete admin CRUD/forms/sessions, legacy DTO/endpoint adapters, task/reservation/profile persistence, notification/settings/export functions, and **all custom billing/parking/payment/paid-energy-stop processing**.

The transactional outbox only captures raw events. It has **no delivery worker** and must not be represented as working billing integration. Implement and test the documented nine billing calls and six billing SQL workflows with business fixtures. Avoid automatic HTTP retries of financial operations until the receiving API provides idempotency and reconciliation.

Replay, outbox and audit tables require a reviewed retention/archival policy. No automatic purge is provided: removing replay records too soon can duplicate effects on late retries. Partition/prune legacy telemetry only after measuring its access patterns. The station-row lock deliberately favors consistency; workers still execute blocking SQL, and the architecture is not a demonstrated maximum-throughput design. Multi-instance routing, HA, tenant-aware identity, credential storage/rotation, detailed audit, security-profile 3, and disaster recovery are not complete.

Production release requires parity against representative charger traces, financial reconciliation, Windows and Linux system tests, ASan/UBSan/fuzz runs, steady-state and soak benchmarks against Java, an SBOM/dependency vulnerability review, and independent pentesting of the actual deployment. No pentest pass or OCPP certification is claimed.

Upstream dependencies retain their licenses. The MariaDB connector is linked dynamically; ship the applicable notices and satisfy its LGPL obligations. The legacy project's missing license/header files also need ownership/license review before redistribution.
