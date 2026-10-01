# Team development

Read [architecture](docs/ARCHITECTURE.md), [parity](docs/PARITY.md) and [validation](docs/VALIDATION.md) before changing runtime behavior. These are project conventions, not a universal coding protocol. Use small changes with a clear owner and reviewable purpose.

## Code conventions

Use C++20 and the checked-in `.clang-format` (LLVM base, four spaces, 100 columns). Public declarations go in `include/ocpp/<module>/`; implementation and private helpers stay in that module. Include public headers by their full `ocpp/...` path and include what a file directly uses. Keep headers self-contained. Prefer RAII, value types, explicit ownership and bounded collections; avoid manual resource cleanup and hidden work in constructors.

Group code by responsibility and use case. Separate validation from IO where practical. Add comments for invariants, constraints and non-obvious decisions, rather than narrating statements. Keep transport-specific types out of public application interfaces. Do not solve an unrelated naming or formatting issue in a behavior change.

Use prepared statements and existing transaction primitives. Preserve replay, ownership, locking and atomicity guarantees. Validate untrusted values at the boundary and protocol layer. Never log tokens, private keys, passphrases, payment secrets or connection passwords. Do not commit `config.local.json`, local certificates, database dumps or `.deps/` artifacts.

## Changes and review

1. Describe the trigger and resulting behavior, affected module and compatibility constraints.
2. Implement the smallest coherent change. Keep database migrations additive unless a migration/rollback plan explicitly requires otherwise.
3. Format changed C++ files and run the boundary check, build and core checks.
4. Run isolated integration tests for changes affecting commands, authentication, persistence, SOAP or WebSocket handling. Run browser checks for UI changes. Measure performance when changing a hot path; record workload and limits.
5. Update protocol coverage, API/UI documentation and validation evidence when behavior changes. Request review with the actual checks run and remaining limits.

Review transaction scope, error/timeout/disconnect behavior, restart recovery, authorization, bounds, secret redaction and Windows/Linux portability where relevant. A successful local build is not evidence of Linux execution, protocol certification or a pentest.

## Local checks

```powershell
python scripts/check-architecture.py
./scripts/build-windows.ps1
./scripts/test-windows-integration.ps1 -MariaDbDirectory .deps/test-mariadb/mariadb-11.4.5-winx64 -Ui
```

The integration script creates an isolated local test database. Do not run destructive fixtures against the configured remote database. On Linux, build using the README instructions and run `scripts/test-linux-integration.sh`. CI runs module boundaries and Windows core checks, plus Linux sanitizer and isolated integration checks.

Do not add a dependency casually: explain its need, maintenance and license, pin the revision, and keep implementation-specific includes private. If a change alters module boundaries, update CMake, the boundary checker and the architecture document together.
