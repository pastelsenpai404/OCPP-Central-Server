# Migration parity

Status on 2026-10-02: **incomplete**. No production switchover is authorized or performed.

| Legacy area | C++ replacement | Status / release blockers |
| --- | --- | --- |
| OCPP 1.6 JSON envelopes and validation | protocol.cpp, schema files | Implemented subset; certification and charger corpus pending |
| Ten OCPP 1.6 incoming operations | service.cpp | Core SQL implemented; billing branches and vendor behavior not ported |
| Tag authorization | service.cpp | Accepted/Invalid/Blocked/Expired and legacy concurrency policy; RFID billing/Schneider workflows pending |
| Transaction start/stop | service.cpp | Ownership, core rows, replay, semantic dedup, basic status/reservation update; custom financial effects pending |
| Meter values | service.cpp | Values/sample metadata persisted; paid-energy target stop and billing thresholds pending |
| Charge-point commands | server.cpp | Seventeen commands; firmware/diagnostics destination policy pending; reservations/profiles/local-list task-result persistence pending |
| WebSocket sessions and ping | server.cpp + pinned transport patch | Single-process sessions, masked frames, fragment handling/limits, timers; full RFC6455/OCPP suite and soak pending |
| OCPP 1.2 JSON | none | Not ported; explicitly rejected |
| OCPP 1.5 JSON | none | Not ported; explicitly rejected |
| OCPP 1.2/1.5/1.6 SOAP + WS-Addressing | none | Not ported |
| Java/jOOQ repository model | mysql.cpp, service.cpp | Core prepared statements; complete repository CRUD/views/filters/exports pending |
| `/dev` API and legacy REST DTOs | new authenticated REST routes | Contracts changed; original routes and JSON shapes not reproduced |
| JSP administration and forms | none | Not ported; includes profiles, reservations, users, tasks, settings, logs, signin/out |
| Authentication and Spring sessions | fixed high-entropy role tokens | New non-browser interface only; user accounts, per-person roles, tenant isolation and secure browser login pending |
| Charging profiles, local lists, reservations | schema validation / small reservation update | Full business lifecycle and durable command task state not ported |
| Billing HTTP calls | raw transactional event outbox | Nine receiving calls not delivered; outbox worker, idempotency and reconciliation pending |
| Billing SQL/parking/payment/paid-energy calculations | none | Six DB workflows and financial arithmetic not ported |
| Settings and notification/mail | heartbeat interval read only | Settings write, notification rules, mail and internet/release probes not ported |
| Background cleanup/jobs and unknown station/tag tracking | none | Not ported; require bounded persistence and retention decisions |
| Windows native execution | MSVC Release build + isolated MariaDB tests | Verified for tested cases only |
| Linux native execution | CMake + sanitizer CI definition | Not executed here |
| Enterprise operations | example TLS gateway/systemd service + counters | Not deployed; HA, backup recovery, alerts, identity, audit and SBOM work pending |
| Performance | heartbeat parse+schema microbenchmark | Full Java comparison, mixed workloads, RSS, p99 latency and soak pending |
| Security assurance | security regression checks | Independent deployment pentest, dependency audit and OCA certification not performed |

`java-inventory.csv` lists every original Java source file and its category. It is a scope inventory, not a claim that all those classes were translated. There are no placeholder implementations that report success for missing protocols or billing effects.

## Critical billing references

Use `../../docs/external-endpoints.md` to inventory endpoint paths without copying legacy credentials. Relevant original sources:

- `service/OcppTagService.java`: card authentication, billing station lookup, Schneider event cleanup.
- `service/CentralSystemService16_Service.java`: connector status transitions, parking/receipt/preparing/suspend-EV flows.
- `repository/impl/OcppServerRepositoryImpl.java`: connector creation, transaction initialization/completion, paid-energy stop and billing calculations.
- `web/controller/ApiController.java`: start/stop/change-configuration, user/token functions, existing external response contracts.

The existing database reference dumps describe structure, not valid payment fixtures. Port money/energy arithmetic with explicit units and decimal precision; reconcile representative outcomes against the original before enabling external calls. Newly generated C++ protocol events cannot be assumed to contain all values needed by the old billing endpoints.

## Release evidence still needed

1. Map and exercise every public route, protocol action and business workflow against the Java implementation.
2. Run full database integration against a sanitized copy of the deployment schema, including deadlocks, restarts, network failures and migrations.
3. Prove billing exactly-once effects through receiver idempotency and reconciliation, including partial failure and replay.
4. Test Linux and Windows packaging and upgrades, credential rotation, gateway TLS, authorization and disaster recovery.
5. Benchmark identical workloads against Java; report hardware, concurrency, mixed traffic, SQL/TLS settings, p50/p95/p99, RSS and errors.
6. Run compiler sanitizers, fuzzing, dependency/SBOM review and independent deployment pentesting. Resolve findings before release.
