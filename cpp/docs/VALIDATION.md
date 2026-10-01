# Validation record

2026-10-02. These results describe the implemented C++ subset, not feature parity, OCPP certification, pentest completion, or production readiness.

## Local build and regression checks

- Windows 11 Home 10.0.26200 x64, Visual Studio 2022 Build Tools, MSVC 14.44.35207, CMake, Release mode.
- Own C++ targets compile with `/W4 /WX /sdl /guard:cf`; dependency warning settings differ.
- CTest: core executable passed **94 assertions**, covering malformed messages, duplicate JSON members, size/depth bounds, schema constraints, timestamps, authorization roles, rates, queue capacity and FIFO order.
- Isolated MariaDB 11.4.5 integration: **544 assertions passed**, against checked-in legacy schema definitions without data and all six additive migrations.
- Cases include missing credentials/wrong subprotocol, reader/admin permissions, pagination, boot and heartbeat, all ten incoming actions, tag blocking and authorization replay after revocation, transaction/meter/status persistence, same-ID replay, semantic start/stop deduplication, conflicting IDs, cross-station denial, outbox atomicity, outbound command success/invalid reply/CALLERROR/30-second timeout, reconnect replay, duplicate-key rejection, valid fragments with interleaved ping, aggregate fragment limits and unmasked-frame rejection.
- Test databases used newly generated names and credentials on separately started ephemeral loopback ports. These integration tests touch only their temporary database. Separate setup/smoke checks use the newly provisioned, isolated remote C++ database.
- The original Java `src`, `pom.xml` and `docs` have no tracked changes.
- Linux build, sanitizer runs, extended fuzzing, full RFC6455/OCA suites, migrations against real deployment data, and production gateway/service operation have **not** been executed.

Additional current cases cover every 2.0.1 action direction; local-list/profile/reservation persistence; variable-result correlation; late/conflicting transaction events; complete 50-character vendor metadata; legacy JSON conversions; typed SOAP inputs/router and actual outgoing HTTP commands; namespace/correlation/redirect rejection; SOAP Sender/Receiver faults, escaped text, authenticated correlation and malformed-message rejection; RSA/P256 CSR verification and forged/weak key rejection; 512-item bulk inserts; integer sandbox billing and idempotency; prepared-statement reuse; an explicitly killed test DB connection and safe reconnection; and an actual server process crash followed by uncertain task recovery without redispatch.

The actual PowerShell launcher was smoke-tested against the migrated 38-table remote C++ database over pinned SSH and verified DB TLS. Startup/readiness validate the required tables. The original legacy database/service remains separate. The single-console `run-local.ps1` was also exercised with PuTTY key authentication against that DB and HTTP readiness 200; a native Windows Ctrl+C event exited successfully and released the process tree. Terminating the launcher independently released both owned process trees through its Windows job. An occupied-port check from another working directory failed before opening SSH.

## Administration UI checks

The Release executable embeds the Control room HTML/CSS/JavaScript. Headless Microsoft Edge/Playwright exercised the dashboard and each navigation view, invalid-token login, reader/operator/admin menus, every command editor (1.2/1.5/1.6/2.0.1), cancelled review, successful mock SOAP Reset with command ID, tag/token/group provisioning, sandbox settlement, untrusted tag text and a 390-pixel viewport. No uncaught browser JavaScript errors were observed. These UI writes used only the ephemeral integration database, not the remote DB. The 544 integration assertion total includes these browser checks.

API regression separately checks missing authentication, admin-only id-token reads, same-origin Bearer POST, foreign-origin denial without DB side effects, catalog action counts, and restricted session metadata. Core tests still passed 94 assertions. Remote smoke checks confirmed the embedded UI returns HTTP 200 with security headers and its authenticated session returns CP01 metadata; the remote sandbox ledger remained empty.

Browser tokens are retained only in page memory; no localStorage/sessionStorage/cookie persistence is implemented. The UI is scoped to the existing local setup. These tests do not establish pentest completion or full legacy UI/business parity. See ADMIN-UI.md.

## Theme update

The light theme was checked in headless Microsoft Edge at 1440?1024 and 390?844 using a separately staged Release executable and the already active verified remote DB tunnel. Login, dashboard, stations, transactions, command center, access, sandbox view, system and sign-out were exercised without writes. Screenshots of the login, dashboard, commands and mobile views were visually reviewed; neither viewport produced horizontal page overflow and no uncaught JavaScript errors were observed. Navigation icons are local inline SVG paths; no external font/icon service is used. Reduced-motion preferences disable transitions.

The tested executable was copied to the normal launcher path and its SHA-256 compared with the staged executable. The existing user's process was preserved by renaming its old executable into an ignored build backup; it continues to render its previous embedded theme until restarted. The temporary build output override was removed afterward. No new full protocol regression or performance measurement was required for this visual change; the earlier 94/544 results describe their earlier run.

## Module restructuring validation

The modular source/header layout, separate CMake implementation libraries, split server adapters and relocated test runners were rebuilt with Windows MSVC Release warnings-as-errors. The executable remains at `build/Release/ocpp_server.exe`; launcher/config/schema contracts are unchanged.

- Core executable: **94 assertions passed**.
- Architecture checker: **40 C++ files passed**; also registered with CTest and both CI jobs.
- Isolated MariaDB integration with headless Edge administration checks: **544 assertions passed**, including JSON/WebSocket, legacy SOAP, durable commands, sandbox billing and UI scenarios.
- `git diff --check`: passed.

Linux targets and CI commands were updated, but Linux execution was not performed for this restructuring. Performance was not remeasured; the older samples below are not results for the refactored binary.

## Performance samples

Host CPU: Intel Core i5-14400F, 10 cores / 16 logical processors. Client, C++ server and temporary MariaDB all run on the same Windows host; the load client is Python asyncio/websockets. Other machine workloads and storage conditions were not controlled.

| Measurement | Result | Scope |
| --- | --- | --- |
| Heartbeat parse + schema microbenchmark | 305,476 messages/s; 3.274 microseconds/message | 100,000 iterations; excludes network, SQL, TLS, billing and concurrency |
| Final loopback load | 100 connected stations, 5,000 completed Heartbeats | 50 per station at a deliberately paced 10 requests/s/station |
| Completed rate / elapsed | 1,002.34 messages/s / 4.988 s | **Offered-load result, not saturation capacity** |
| End-to-end p50 / p95 / p99 | 13.362 / 28.630 / 37.960 ms | WebSocket plus MySQL station-row lock, heartbeat update and commit; four workers |
| Errors | 0 | Only this short test workload |

An earlier version persisted replay records for Heartbeat and sampled 996.05 messages/s with p99 58.286 ms under the same offered pacing. Heartbeat, boot and authorization now always return current state/time; unnecessary durable replay queries/writes for these actions were removed. The current sample also uses a bounded 64-entry prepared-statement cache per DB connection and 64-row meter batches. These are individual samples, not statistically proven performance gains.

No comparison with Java, maximum-throughput experiment, mixed transaction/telemetry workload, WSS/TLS measurement, billing throughput, RSS measurement, high availability test, or soak has been completed. Do not extrapolate this five-second sample to fleet capacity or enterprise SLAs.

## Dependency review

OSV source-commit queries returned two Drogon ORM advisories and no advisories for the other seven final pins at lookup time. The relevant ORM APIs are not called by this application; this is an applicability assessment, **not a clean dependency scan**. See DEPENDENCIES.md for details and primary-source links.

zlib was upgraded to 1.3.2. The executable and connector PE import tables both reference `z.dll`; a runtime `zlibVersion()` check returned **1.3.2**. Connector/C is configured to use this external library instead of its bundled older zlib. This source review is not an exhaustive transitive binary vulnerability/SBOM assessment.

No independent pentest has been run. No claim is made that this code passes all pentests or is secure against every attack.

Temporary test data directories remain under ignored `.deps/`: automatic command review rejected optional recursive cleanup as "blocked by policy". Test processes have stopped and temporary named test databases were dropped by the test harness. Package only the whitelist of current executable/DLL/schema artifacts; do not redistribute `.deps/` or a whole reused build directory.
