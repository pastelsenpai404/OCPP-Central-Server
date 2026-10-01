# Validation record

2026-10-02. These results describe the implemented C++ subset, not feature parity, OCPP certification, pentest completion, or production readiness.

## Local build and regression checks

- Windows 11 Home 10.0.26200 x64, Visual Studio 2022 Build Tools, MSVC 14.44.35207, CMake, Release mode.
- Own C++ targets compile with `/W4 /WX /sdl /guard:cf`; dependency warning settings differ.
- CTest: core executable passed **56 assertions**, covering malformed messages, duplicate JSON members, size/depth bounds, schema constraints, timestamps, authorization roles, rates, queue capacity and FIFO order.
- Isolated MariaDB 11.4.5 integration: **60 assertions passed**, against the actual sibling legacy schema definitions and additive runtime migration.
- Cases include missing credentials/wrong subprotocol, reader/admin permissions, pagination, boot and heartbeat, all ten incoming actions, tag blocking and authorization replay after revocation, transaction/meter/status persistence, same-ID replay, semantic start/stop deduplication, conflicting IDs, cross-station denial, outbox atomicity, outbound command success/invalid reply/CALLERROR/30-second timeout, reconnect replay, duplicate-key rejection, valid fragments with interleaved ping, aggregate fragment limits and unmasked-frame rejection.
- Test databases used newly generated names and credentials on separately started ephemeral loopback ports. No deployment database was read or modified.
- The original Java `src`, `pom.xml` and `docs` have no tracked changes.
- Linux build, sanitizer runs, extended fuzzing, full RFC6455/OCA suites, migrations against real deployment data, and production gateway/service operation have **not** been executed.

## Performance samples

Host CPU: Intel Core i5-14400F, 10 cores / 16 logical processors. Client, C++ server and temporary MariaDB all run on the same Windows host; the load client is Python asyncio/websockets. Other machine workloads and storage conditions were not controlled.

| Measurement | Result | Scope |
| --- | --- | --- |
| Heartbeat parse + schema microbenchmark | 307,636 messages/s; 3.251 microseconds/message | 100,000 iterations; excludes network, SQL, TLS, billing and concurrency |
| Final loopback load | 100 connected stations, 5,000 completed Heartbeats | 50 per station at a deliberately paced 10 requests/s/station |
| Completed rate / elapsed | 999.45 messages/s / 5.003 s | **Offered-load result, not saturation capacity** |
| End-to-end p50 / p95 / p99 | 17.243 / 38.038 / 48.396 ms | WebSocket plus MySQL station-row lock, heartbeat update and commit; four workers |
| Errors | 0 | Only this short test workload |

An earlier version persisted replay records for Heartbeat and sampled 996.05 messages/s with p99 58.286 ms under the same offered pacing. Heartbeat, boot and authorization now always return current state/time; unnecessary durable replay queries/writes for these actions were removed. These are individual samples, not statistically proven performance gains.

No comparison with Java, maximum-throughput experiment, mixed transaction/telemetry workload, WSS/TLS measurement, billing throughput, RSS measurement, high availability test, or soak has been completed. Do not extrapolate this five-second sample to fleet capacity or enterprise SLAs.

## Dependency review

OSV source-commit queries returned two Drogon ORM advisories and no advisories for the other six final pins at lookup time. The relevant ORM APIs are not called by this application; this is an applicability assessment, **not a clean dependency scan**. See DEPENDENCIES.md for details and primary-source links.

zlib was upgraded to 1.3.2. The executable and connector PE import tables both reference `z.dll`; a runtime `zlibVersion()` check returned **1.3.2**. Connector/C is configured to use this external library instead of its bundled older zlib. This source review is not an exhaustive transitive binary vulnerability/SBOM assessment.

No independent pentest has been run. No claim is made that this code passes all pentests or is secure against every attack.

Temporary test data directories remain under ignored `.deps/`: automatic command review rejected optional recursive cleanup with “blocked by policy”. Test processes have stopped and temporary named test databases were dropped by the test harness. Package only the whitelist of current executable/DLL/schema artifacts; do not redistribute `.deps/` or a whole reused build directory.
