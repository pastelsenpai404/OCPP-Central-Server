# Migration parity

Status on 2026-10-02: the complete Java application migration remains **incomplete**. No production charger/billing switchover has been performed. Billing is restricted to an isolated sandbox, as requested.

| Area | Current C++ implementation | Remaining work |
| --- | --- | --- |
| OCPP 1.6 JSON | 14 incoming and 26 outgoing actions, including security extensions; strict schemas and direction checks | Charger corpus and independent OCA/security validation |
| OCPP 2.0.1 JSON | 64 action pairs, 25 station-initiated actions and 40 CSMS commands with DataTransfer overlap; all directions exercised in integration | Full feature conformance, real chargers and positive certificate-provider paths |
| OCPP 1.2/1.5 JSON | Version negotiation, WSDL-derived validation, normalized meters/status/boot and outgoing local-list spelling | Further vendor interoperability; configured bounds can reject oversized legacy fields |
| OCPP 1.2/1.5/1.6 SOAP | SOAP 1.2 typed bodies, namespace/router checks, WS-Addressing, Basic authentication and explicit outgoing destinations | Per-action charger corpus and full addressing interoperability; errors return bounded SOAP 1.2 faults with safe correlation |
| Transactions | Ownership and 1.6 replay/semantic dedup; 2.0.1 sequence log, late offline events and end-state protection | Physical charger reconciliation and legacy financial branches |
| Meter persistence | Bounded 64-row prepared batches, total sample/message bounds, native units/metadata retained | Mixed fleet/TLS soak and billing metrology validation |
| Commands/state | Durable tasks/audit, reply validation, local lists/versions, reservations/profiles and uncertain restart recovery without resending | Fleet orchestration, multi-process fencing and reconciliation |
| PKI | CSR proof of possession/key-strength checks, PendingReview queue and device certificate commands | Trusted issuance/CA workflow, certificate authorization, OCSP and ISO 15118 contract/EXI providers; unconfigured positive paths fail closed |
| 2.0.1 reports/device model | Durable report chunks/events, variable attributes and EVSE/security/firmware/log information | Complete report assemblies, request reconciliation and smart-charging optimizer |
| Billing sandbox | Explicit THB tariffs, integer milliWh/minor currency arithmetic, parking grace and idempotent simulation ledger | Legacy six SQL/nine HTTP workflows, paid-energy stop, payment fixtures, receipts, reconciliation and gateways |
| Admin/API application | Responsive Control room UI for implemented REST resources, role separation, schema-based commands, access administration, current-page export and task/2.0.1/sandbox views | Legacy /dev/JSP parity, full CRUD/global filtering/export, per-person/tenant identity and remaining backend workflows |
| Settings/jobs/notifications | Heartbeat settings, timers and command recovery | Full settings management, mail/rules, retention and unknown-station/tag workflows |
| Database operations | Isolated MariaDB 10.11.18, verified TLS/pinned SSH, restricted app login and backup/restore checks | Extracted-package upgrades, alerting and backup retention |
| Windows | MSVC Release build, core/integration tests and PowerShell launcher | Service installation/upgrades and long soak |
| Linux | CMake dependencies and isolated DB/sanitizer CI definitions | Linux execution has not run here; neither workstation nor small live DB host has a build toolchain |
| Assurance | Compiler hardening, bounded transports/XML/SQL, security regression and source advisory lookup | Independent pentest, fuzzing/sanitizers, complete SBOM, Java comparison, mixed-load saturation and HA tests |

Schema coverage and successful message dispatch are not certification or complete business-feature parity. java-inventory.csv remains the original source inventory. Original Java sources are unchanged.

Original billing references: service/OcppTagService.java, service/CentralSystemService16_Service.java, repository/impl/OcppServerRepositoryImpl.java and web/controller/ApiController.java. The settlement sandbox defines a new explicit arithmetic contract; it does not reproduce their floating-point financial side effects or execute existing payments. Reconcile those workflows with sanitized payment fixtures before connecting them.
