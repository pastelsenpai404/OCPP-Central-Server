# Control room

Run `run-local.ps1` and visit `http://127.0.0.1:5003/` (or `/admin`). Restart an already running server after updating its executable. The frontend is compiled into the C++ executable, so no Node server, npm install or separate asset directory is required at runtime.

Sign in with the appropriate `OCPP_READ_TOKEN`, `OCPP_OPERATOR_TOKEN` or `OCPP_ADMIN_TOKEN` from the private `config.local.json`. The UI never receives that configuration or station passwords. Enter only the token value. Tokens stay in JavaScript memory, are sent as Bearer headers, and are discarded on sign-out/reload; cookies, localStorage and sessionStorage do not hold them. Do not share the configuration file.

| View | Operations | Minimum role |
| --- | --- | --- |
| Overview | Runtime counters, registered stations, live WebSocket sessions, recent admin audit activity | Reader |
| Stations | Search/export current page, station state, registration of already configured station IDs | Reader; registration Admin |
| Transactions | Paginated OCPP 1.x and 2.0.1 records and details | Reader |
| Command center | Schema-based form or advanced JSON, review, dispatch and correlated result | Operator; certificate/network changes Admin |
| Command history | Paginated durable tasks, exact task lookup and request/result details | Operator |
| Access & tags | Read tags; create/update/block tags; token types, status, expiry and group token | Reader; writes/token reads Admin |
| Connector status | 1.x connector status and 2.0.1 EVSE status | Reader |
| Reservations | Reservations from both protocols; 2.0.1 profile assignments | Reader |
| Device reports | Report chunks, device variables and station metadata | Admin |
| Security | Security events and pending CSR records | Admin |
| Audit log | Paginated stored application audit events | Admin |
| Billing sandbox | Explicit tariff settlement calculator and immutable simulation ledger | Admin |
| System | Runtime counters, endpoint and protocol information | Reader |

Command forms cover the supported CSMS directions (26 for 1.6 and 40 for 2.0.1). Legacy action catalogs use canonical 1.6 payload fields because the server converts commands to their 1.2/1.5 wire representation. Nested objects/arrays use JSON fields; advanced mode edits the complete payload. The backend validates schema, role, station ownership, transfer-origin policy and command semantics before dispatch. Changing a protocol selection does not renegotiate the station's existing connection. Choose its negotiated version; offline stations require a configured SOAP endpoint.

Every write opens a review dialog. Commands can affect a real charger attached to this DB/server. `Uncertain` means that dispatch may have happened but the result cannot be established; inspect the device before retrying. A transport timeout is not permission to resend. Certificate/network task reads redact stored network credentials.

Tables load 100 records at a time. Search and CSV export cover the currently loaded page, not the entire database. Overview station counts refer to the first page; connection status is an instantaneous WebSocket snapshot, not a heartbeat-based fleet SLA. Refresh explicitly to update the view. SOAP configuration is not a live-connection guarantee.

The UI allows authenticated same-origin browser API requests. Foreign origins and cross-site fetches are denied; WebSocket charger endpoints continue to deny browser origins. Assets have a restrictive CSP and use text DOM operations for untrusted values. No externally hosted scripts/fonts are loaded. Local operation checks the actual HTTP connection scheme and Host header. Behind Nginx, deployment sets `OCPP_PUBLIC_ORIGIN` to an exact HTTPS origin and requires its matching Host header; forwarded scheme/host headers cannot authorize a foreign origin. See [deployment instructions](DEPLOYMENT.md).

This is a working UI for the implemented backend. Full legacy JSP/dev API parity, tenant/user identities, automatic PKI issuance, assembled reports, payment gateways and fleet optimization remain separate migration work. Configuration, credentials, backup jobs and process lifecycle are managed through the existing local configuration/PowerShell tools, not through this UI.

## UI regression

The optional browser suite uses a separately initialized, random local test database and mock SOAP chargers. It never writes its test tags, tokens or billing cases to the remote DB.

```powershell
py -m pip install playwright
./scripts/test-windows-integration.ps1 -MariaDbDirectory ./path/to/test-mariadb -Python python -Ui
```

Microsoft Edge must be installed for this Windows browser suite. It covers bad token login, role menus, token storage, all command editors, same-origin writes, cancelled review, actual mock-device command response, tag text escaping, sandbox settlement and narrow viewport rendering. The regular integration suite covers foreign-origin rejection and API-side roles independently of the UI.
