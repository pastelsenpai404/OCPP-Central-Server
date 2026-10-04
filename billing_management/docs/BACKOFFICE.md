# Management backoffice

Reference: `material/EV_NIA_Backend_Backoffice_Billing/src/routes` and its DTOs/models.
This implementation adapts its main staff workflows to C++ and a SvelteKit/WASM UI.
It does **not** reproduce the Go application's API routes or import its private `.env`,
credentials, uploaded files, binary images or production database.

| Reference area | Implemented management workflow |
| --- | --- |
| company, project, station, wizard | Linked company → project → station forms; archive protection and restoration |
| charger, connector | Registry sync to CSMS through a persistent outbox; protocol/model/power/connector metadata and unique connector numbers |
| users | Customer records and active/suspended state |
| pricing/configuration | Flat integer satang/kWh tariff and VAT snapshot per session |
| wallet | Append-only test top-up, withdrawal and bill payment, balance protection |
| charging history/refund | Manually recorded postpaid test sessions; bounded pre-invoice price adjustments |
| bill | UTC date-cycle generation, unbilled-session selection, fixed invoice lines, manual/wallet settlement |
| tax invoice | One TEST-TAX document per paid bill, printable to PDF |
| overview/maintenance | Counts, energy/amount summary, recent activity and maintenance records |
| admin/rank | Staff registry; actual API role tokens: reader, operator and admin |
| log/trash | Append-only mutation audit, optimistic versions, archive/restore |
| exports | CSV for current table page with spreadsheet-formula neutralization |

Not yet implemented: username/password sessions, organization/project-scoped permissions,
MFA/OTP/email reset, multi-tenant isolation, real CSMS telemetry/commands or ingestion,
TOU calendars, monthly station revenue-share/partner settlement, post-payment credit notes,
bank/payment gateways, legally compliant tax issuance, partner APIs, S3/file uploads,
bulk Excel/PDF reports or automatic billing cron. Settings records are business metadata,
not server secret storage. No pentest or OCPP certification claim is made.

## Run and access

- Server deployment: root `./deploy.ps1 -Target Billing` builds both billing frontends,
  builds/tests the C++ APIs, activates systemd and updates the four Nginx HTTPS domains.
- Management UI: `https://ev.admin.barryofeverything.com`.
- Management API: `https://ev.admin.api.barryofeverything.com`.
- Tokens: `/etc/billing-management/management.env`, root-readable only. Existing admin
  token is preserved; new distinct reader/operator tokens are generated on deployment.
  Use `BILLING_API_TOKEN` for full management access. Never commit/copy tokens into UI assets.
  On Windows run `./get-management-access.ps1` to fetch it into ignored
  `management/access.local.json`; copy `api_token` into the UI's Access token field.
  The script restricts the file to your Windows account and never prints the token.
- SQLite database: `/var/lib/billing-management/backoffice.sqlite3`, owned by the
  management service. Customer API cannot access it. It persists across deployments.
  It is hosted on the server and accessed through the HTTPS API; no database port is exposed.
- Before subsequent activations a consistent online backup is written to
  `/var/backups/billing-management/<release>.sqlite3`, private to root. Backups require an
  operational retention policy; deploy does not prune them or automatically restore a live DB.
- Windows/local: `./run-management.ps1` launches the C++ API and frontend using ignored
  `management/config.local.json`, with a separate local sandbox database. Alternatively
  `./scripts/run-frontend.ps1 -Area management` can connect to the hosted management API.
  Local-origin access to the hosted API is intentionally denied by its production CORS policy;
  use the deployed frontend for hosted access, or explicitly configure an allowed dev origin.

## Contracts and money rules

All staff API routes start with `/api/v1/management`.

- GET `/me`, `/modules`, `/overview`, `/audit`, `/trash`.
- GET/POST `/{module}`; GET/PATCH/DELETE `/{module}/{id}`; POST `/{module}/{id}/restore`.
  Modules: companies, projects, stations, chargers, connectors, customers, tariffs,
  maintenance, admins and settings. PATCH replaces editable fields and requires `version`;
  DELETE/restore also require the current `version` to prevent stale writes.
- GET `/sessions`, `/refunds`, `/bills`, `/tax-invoices`, `/wallet-events` and `/{id}` details.
- POST `/sessions`: customer_id, connector_id, energy_wh, reference.
- POST `/sessions/{id}/refund`: amount_satang, reason. This reduces an **unpaid/unbilled**
  postpaid session's future invoice; it does not also credit the wallet. Already billed
  sessions are rejected pending a separate credit-note workflow.
- GET `/wallet/{customer_id}`; POST `/wallet/top-up` or `/wallet/withdraw`:
  customer_id, amount_satang, reference.
- POST `/bills`: customer_id, period_from, period_to (inclusive UTC `YYYY-MM-DD`).
  At most 500 previously unbilled sessions; retrying with a new key cannot bill them twice.
- POST `/bills/{id}/pay`: method (`manual` or `wallet`), reference.
- POST `/bills/{id}/issue-tax`: empty JSON object; only paid bills, one document per bill.

POST financial actions require `Idempotency-Key` (16–100 alphanumeric/underscore/hyphen
characters). Matching retries return the stored result; changed payloads are rejected.
State, ledger, audit and idempotency result commit atomically. The UI preserves its
key and payload after an uncertain response; close/reopen to intentionally create a new request.

Currency is THB. Energy is integer Wh; unit prices and balances are integer satang.
Energy subtotal rounds half up: `(Wh * satang_per_kWh + 500) / 1000`.
Pre-bill adjustments reduce the line subtotal. VAT rounds half up **per session line**:
`(net_satang * vat_percent + 50) / 100`. Bill totals sum the rounded line values.
These are documented sandbox rules, not a validated production tax policy.

Reader may read; operator may maintain normal records and run sandbox finance actions;
admin may additionally archive/restore and maintain staff/settings. Role tokens represent
shared service roles, not individual user identities; audit identifies that role.

## Management and OCPP registry

Creating a charger queues its `code` as the OCPP `chargeBoxId` in the same SQLite
transaction. A background worker calls the CSMS admin-only registry endpoint on
loopback. Network failures retain the job across restarts and retry with backoff;
the charger table shows pending, retrying or synced and offers **Sync OCPP**.
Existing active chargers are also queued when this version starts. Charger codes
cannot change or be reused after archiving. Archiving a business record keeps its
CSMS registry and historical transactions intact.

Deployment reads the existing server-side OCPP admin token into the management
service environment (`BILLING_OCPP_TOKEN`, `BILLING_OCPP_HOST`, `BILLING_OCPP_PORT`).
It is never returned to the browser. Without a configured integration token, local
jobs remain pending. The network destination is fixed to `127.0.0.1`.

Registry creation does not provision a station credential or establish a WebSocket
connection. The existing CSMS credential authentication remains required. The UI
reports whether a credential existed when registration completed; repeat sync after
changing station configuration to refresh this flag. Business status is metadata;
it is not live CSMS telemetry.

## Realistic sandbox data

`scripts/seed-demo.py` adds a linked 30-day dataset to an existing management DB:
two companies, four projects, eight sites, sixteen chargers, thirty-two connectors,
sixty customers, wallet ledgers, variable daily sessions, adjustments, paid/unpaid
bills, test tax documents and maintenance jobs. All identifiers use `DEMO-`, contacts
are synthetic and generated tax documents remain sandbox-only. Charger rows use the
same outbox as normal API creation. The script does not contact a payment provider.

Run on the server after deploying this version:

```sh
python3 /opt/billing-management/build-source/scripts/seed-demo.py \
  --database /var/lib/billing-management/backoffice.sqlite3 \
  --backup /var/backups/billing-management/before-demo-UNIQUE.sqlite3
```

Choose a new backup filename. The script takes an online SQLite backup before a
single atomic insert transaction, retains existing records and records its manifest.
Repeating the same seed is harmless; changed options or colliding demo identifiers
are rejected. No automatic destructive reset is supplied.

## Verification

`BILLING_BUILD_HTTP=ON` registers an isolated API integration test with CTest. It creates
its own temporary DB/tokens/listener, exercises the full financial lifecycle, concurrent
payment retries, authorization, malformed input, stale edits and restart persistence.
It never mutates the deployed database. Browser checks use Playwright/Edge and production
static assets:

```powershell
py -3 tests/integration/backoffice_smoke.py --binary build/http/management/backend/Release/billing_management_api.exe
py -3 tests/integration/registry_seed_smoke.py --binary build/http/management/backend/Release/billing_management_api.exe
py -3 tests/integration/backoffice_browser.py --binary build/http/management/backend/Release/billing_management_api.exe
```

Linux executable paths omit `Release` and `.exe`. Browser dependencies are listed in
`tests/requirements-browser.txt`; screenshots are ignored under `.deps/backoffice-browser`.
