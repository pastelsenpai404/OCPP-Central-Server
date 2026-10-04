# Working on the simulation lab

Keep application state and OCPP lifecycle in C++; UI renders snapshots and sends intent.
Domain physics must remain independent of HTTP, Windows, Linux and protocol libraries.
Transport adapters own their connection/thread lifecycle. Do not pass browser-provided URLs
to network clients or place credentials in UI bundles, traces, logs, fixtures or git.

Use C++20, RAII, bounded collections, explicit unsupported responses and typed frontend
API models. JSON/request validation belongs at the HTTP/protocol boundary. Handle an
unconfirmed start separately from an established transaction; never silently start billing
twice after reconnect. Do not add commands claiming support without meaningful lifecycle tests.
Format C++ using the checked-in .clang-format before review.

Run native CTest, frontend type/build checks and the isolated workflow suite for changes
to lifecycle/protocol/transport. Run Windows and Linux builds for transport changes.
Deployment tests must not create transactions on a production CSMS. Temporary test processes
must be stopped in finally blocks; deployment rollback must preserve previous config.

Changes to persisted state (currently none), protocol support, simulation assumptions,
toolchain versions and deploy domains need corresponding README updates.
