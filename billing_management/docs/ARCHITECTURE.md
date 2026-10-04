# Architecture

## Ownership and dependencies

Management owns staff tariff administration, invoice operations, reconciliation and audit.
Customer owns the customer's charging history, invoices and payment status views.
Management's sandbox capabilities are implemented; customer remains a preview. See BACKOFFICE.md for the reference mapping and remaining external integrations.

Backend dependency direction:

```text
interfaces (CLI + HTTP) -> application -> domain
infrastructure -> application ports + domain
composition root -> concrete adapters
```

- `include/billing/<area>/application`: public use-case interfaces.
- `src/application`: orchestration; define repository/payment interfaces here when needed.
- `src/domain`: rules unique to this application, with no transport or SQL dependency.
- `src/infrastructure`: concrete storage, payment and CSMS adapters.
- `src/interfaces`: input parsing, authorization adapters and response mapping.
- `shared/domain`: narrowly shared pure C++ rules. It has no database, HTTP or browser dependencies.
- `shared/contracts`: explicitly versioned inter-product API/event definitions. Share schemas, not database tables or private implementation headers.
- `shared/transport`: bounded loopback HTTP adapter, with application quote handlers
  injected by each executable. Separate environment configuration supplies per-app
  bearer tokens, exact API Host and allowed frontend origin. Nginx terminates TLS.

Each backend has its own executable and application target. Neither may include the
other backend's headers or `ocpp_csms` source. Both use `billing_domain`, which is also
compiled for WASM. Frontend presentation stays under each frontend's `src`; shared
browser code and the C/WASM boundary live under `shared/frontend`.

Both frontends are SvelteKit applications, built as prerendered static sites with
client hydration. Route files own application composition; the shared `@billing/ui`
npm workspace owns Svelte components, styles and the typed WASM loader. Local facades
under `src/lib/wasm` isolate the application from the implementation boundary.
Only `onMount` invokes the browser-specific Emscripten loader. Generated `.js`/`.wasm`
files are staged into `static/wasm`, ignored in Git, and served from `/wasm/`.
Each frontend is intended to run at the root of its own origin. For deployment under
a URL prefix, configure SvelteKit paths and pass the matching asset prefix to the
loader together. Node/npm are build tools; production backends remain C++.

## Billing invariants

The initial quote is an energy subtotal in satang, with half-up rounding at the final
division. Inputs are bounded to 1,000,000,000 Wh and 1,000,000 satang/kWh; multiplication
uses 64-bit integers. The result remains within exact JavaScript integer range. This
preview is not an approved invoice calculation or a VAT policy.

Before implementing persistence: specify currencies, tax/discount ordering, timezone
and tariff effective dates, append-only ledger semantics, idempotency keys and refund
rules. Payment secrets belong only on the server. Customer endpoints must derive
ownership from authentication. Staff writes require distinct permissions and audit.

Management stores isolated sandbox data in SQLite 3.53.4, using prepared statements,
WAL, full synchronous commits and a mutex around each complete business transaction.
The deploy unit owns a private StateDirectory outside releases. The application
depends on the business-facing `application/store.hpp` persistence port. The composition
root injects the SQLite adapter; SQL remains exclusively in infrastructure. Domain module
definitions and validation are separate from storage and HTTP. Schema version 1 uses additive setup.
Financial records are append-only except the controlled issued-to-paid bill transition.
CSMS ingestion, customer ownership and production payment integrations are separate
future adapters, not implied by manually entered charging sessions.
