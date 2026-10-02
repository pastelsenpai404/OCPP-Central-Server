# Architecture

The server is a modular monolith: one deployment, with separately compiled modules and explicit dependencies. This keeps transaction handling local while giving a team clear places to work. Moving files is not a claim of complete Java parity or production readiness; see PARITY.md and VALIDATION.md.

## Responsibility map

| Directory under `src/` and `include/ocpp/` | Responsibility | CMake target |
| --- | --- | --- |
| `protocol/` | Envelopes, schemas, command validation; `v16/`, `v201/`, `legacy/` separate wire versions | `ocpp_protocol` |
| `config/` | Configuration types, environment parsing and startup validation | `ocpp_config` |
| `security/` | Credential comparisons, rate limits, certificate verification | `ocpp_security` |
| `runtime/` | Bounded executors and worker lifecycle | `ocpp_runtime` |
| `persistence/` | MariaDB driver, pool, prepared statements and transaction primitives | `ocpp_persistence` |
| `billing/` | Integer settlement calculations, independent of payment execution | `ocpp_billing` |
| `transport/` | Outbound HTTP implementation | `ocpp_http` |
| `application/` | OCPP workflows, durable command tasks, queries, access updates and sandbox ledger writes | `ocpp_application` |
| `src/server/` only | Drogon transport adapters, sessions and composition | `ocpp_server` |

`src/main.cpp` only delegates startup. `server/bootstrap.cpp` constructs the runtime and starts the server. Server adapters are grouped into `http/`, `websocket/`, `soap/`, and `commands/`; maintenance and shared runtime state have their own files. Public headers live under `include/ocpp/<module>/`. Server internals are private to `src/server/`.

`ui/` owns the embedded administration client. `schemas/` owns protocol contracts, `migrations/` owns additive SQL changes, `deploy/` owns host operations, and `scripts/` owns developer tooling. Private local configuration is not source code.

## Dependencies and boundaries

Application workflows use protocol validation and persistence, plus security and settlement helpers. They do not depend on Drogon. Transport adapters authorize requests, decode inputs, invoke workflows and encode responses; SQL belongs in application workflows or persistence, not routes. Protocol validation does not perform SQL or call the application layer. The MariaDB driver does not depend on application workflows. Settlement arithmetic does not execute a payment or access a database.

`ocpp_types` supplies shared include paths and JSON/schema types. Configuration **types** are available to protocol and persistence without depending on environment parsing. `ocpp_core` is an interface aggregation target retained for executables and tooling; it contains no implementation. Actual module dependencies are declared in `cmake/targets.cmake`.

Run `python scripts/check-architecture.py`. CI checks module includes, missing public headers, implementation includes and placement of Drogon/MariaDB headers. This catches dependency drift, but cannot replace review of behavior, SQL placement or transaction semantics. CTest also runs it when Python is available.

## Workflow and lifetime rules

`Service` remains the application facade. Its implementation is split by use case rather than changing transaction boundaries during this restructuring. Cross-version helper duplication can be extracted when a shared invariant is established; do not introduce a generic base class just to reduce line counts.

Keep station locking, replay checks, ledger/outbox writes and commits in the same application transaction. Preserve uncertain command outcomes after disconnect/restart; do not automatically retry a physical operation. Queue work through bounded executors. `Runtime` owns configuration, schemas, the database, service and sessions; executor destruction drains jobs before their dependencies are destroyed. Do not introduce another global runtime or hide database work in a controller.

## Tests and build

- `tests/unit/`: core protocol, validation, arithmetic and utility checks.
- `tests/integration/`: real server/database scenarios, SOAP device fixture and browser checks.
- `tests/fixtures/`: stable input fixtures.
- `tests/performance/`: performance measurements, separate from correctness assertions.
- `tests/fuzz/`: fuzz entry points.

The executable, schema directory, migrations, environment variables and PS1 launcher paths retain their existing contracts. Dependency pins live in `cmake/dependencies.cmake`, compiler/link hardening in `cmake/hardening.cmake`, and embedded UI generation in `cmake/admin-assets.cmake`.

For a new feature, first locate its application use case and wire contract, then implement validation, transaction changes and transport mapping in their respective modules. Add a migration for schema changes and tests for the invariant being changed. Architecture changes need a short decision record explaining the problem, options, choice and operational consequences; do not create a network service solely to split a folder.
