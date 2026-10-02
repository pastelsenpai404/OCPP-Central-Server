# Billing deployment

The root `../deploy.ps1` deploys CSMS plus billing by default. This product also has
its own `deploy.ps1`. Both use the existing pinned PuTTY SSH host key, root account
on `104.248.96.73` and the ignored key in `../material/`. Pageant prompts for its
passphrase when needed; scripts do not save it.

## DNS in DigitalOcean

Open **Networking → Domains → barryofeverything.com → Create new record**.
Create these **A records**, all with value **104.248.96.73**, TTL **300**:

| Hostname | URL | Destination |
| --- | --- | --- |
| `ev.admin` | `https://ev.admin.barryofeverything.com` | Management SvelteKit site |
| `ev.admin.api` | `https://ev.admin.api.barryofeverything.com` | Management C++ API, loopback 5500 |
| `ev.customer` | `https://ev.customer.barryofeverything.com` | Customer SvelteKit site |
| `ev.customer.api` | `https://ev.customer.api.barryofeverything.com` | Customer C++ API, loopback 5501 |

Keep `ocpp` pointing to the same IP for CSMS. Remove conflicting AAAA/CNAME records
for these names before deploying. All relevant names must resolve directly to this
server. Domain validation happens before live deployment; no certificate is issued
by `-PrepareOnly` on a fresh installation.

## Commands from the repository root

```powershell
./deploy.ps1 -PackageOnly             # Rebuild/check billing frontends; package both products
./deploy.ps1 -Target Billing -PrepareOnly
./deploy.ps1                          # CSMS plus all billing sites, after DNS is ready
./deploy.ps1 -Target Billing          # Billing only
./deploy.ps1 -Target Ocpp             # CSMS only
```

Or from `billing_management`: `./deploy.ps1 -PrepareOnly` / `./deploy.ps1`.
`-SkipFrontendBuild` on the product script reuses existing production output and is
intended for already-verified builds; the root script builds/checks it first.
No database migrations are applied for billing. Root `-ApplyMigrations` affects
only the existing CSMS deployment and must be explicitly requested.

Local prerequisites: PuTTY, tar.exe, C++/Emscripten toolchain, npm and Node 22.17+
(or the Node already installed with the local SDK). The build helper recompiles
WASM, runs quote tests, installs locked npm dependencies, checks Svelte/TypeScript
and builds both static frontends. Only source and public production assets are
packaged; configuration, private keys, node_modules and caches are excluded.

## Linux layout and behavior

- `/opt/billing-management/releases/<id>` contains two API binaries, public static
  sites, dependency licenses, deployment scripts and the archive SHA256.
- `/opt/billing-management/current` selects the active release atomically.
- `billing-management.service` and `billing-customer.service` run as separate
  unprivileged users, with loopback listeners, bounded tasks and memory limits.
- `/etc/billing-management/management.env` and `customer.env` are root-only 0600.
  Each has a distinct generated API token, retained across redeployments.
- Nginx uses `/etc/nginx/sites-available/billing-management.conf` for exactly these
  four names. Existing virtual hosts are preserved. API request limits, exact
  Host/origin checks and frontend CSP hashes are generated for the release.
- Certbot obtains one certificate covering the four billing names and enables
  renewal with a Nginx reload hook. TLS is terminated at Nginx. Wasm is served
  from each frontend's `/wasm/billing.wasm`.
- Preparation serves HTTP 503 except the ACME path on new hosts. If HTTPS is already
  configured, preparation retains it and regenerates frontend CSP for the new HTML.

The script checks port ownership, verifies archive SHA256 and safe members, builds
native C++ on Linux with pinned dependencies, runs CTest, starts both services and
performs 28 direct API smoke checks, followed by 26 HTTPS API checks after certificate
activation. Failed activation restores the previous bundle,
environment files, units and Nginx configuration. Dependency installation, created
Unix users, certificate issuance and inactive release directories are not rolled
back. CSMS and billing releases are independent; root `-Target All` is sequential,
not a cross-product atomic transaction. A Windows-side public connectivity failure
after successful server checks does not undo a healthy server release.

On small hosts the build may use temporary swap, removed afterward without changing
fstab. Native build jobs default to one. The server does not need Node or Emscripten
at runtime; SvelteKit is built locally and deployed as static files.

## Current API scope

These services are **sandbox quote APIs**, not a customer account or payment system:

- `GET /health/ready`: public readiness, service name and sandbox mode.
- `POST /api/v1/quote`: bearer token required; JSON
  `{"energy_wh":1500,"satang_per_kwh":750}` returns `subtotal_satang:1125`.
- CORS accepts only the matching frontend origin, never the other application's
  origin. Bearer tokens remain server/operator secrets and are not bundled into UI.

The UI currently uses local WASM calculations. Customer account authentication,
invoice storage and payments are not implemented. Do not distribute the customer
API's operator token to end users; future customer endpoints need per-user identity
and ownership checks. Billing does not connect to or modify the CSMS database.

## Operations

```sh
systemctl status billing-management billing-customer
journalctl -u billing-management -u billing-customer -n 100
nginx -t
systemctl status certbot.timer
```

After DNS/HTTPS activation, test all four HTTPS URLs and run
`certbot renew --dry-run` to verify renewal against the actual DNS setup.
Rotate a token by updating the corresponding root-only environment file and
restarting that service. Do not commit filled-in configuration; the format is in
`deploy/runtime.env.example`.

## Validation in this workspace

Windows: HTTP binaries build with warnings treated as errors; quote domain checks,
three deployment-generator tests and 28 real API smoke checks passed. The root
`deploy.ps1 -PackageOnly` rebuilt both SvelteKit/WASM sites and packaged both products.
Archive checks found no local CSMS database/API secrets or private key files.

Linux: `-PrepareOnly` installed both APIs; native CTest, 28 API checks and live Nginx
configuration validation passed. An intentional candidate smoke failure restored
the prior release and both active services. Separate loopback-only Nginx instances
using a temporary test certificate passed 26 TLS API checks plus static HTML, CSP
and WASM MIME checks for all four virtual hosts. Browser checks confirmed Svelte
hydration and WASM loading under the generated CSP.

Public billing DNS, Let's Encrypt issuance and renewal dry-run remain pending.
The temporary test certificate is not used by the public Nginx configuration.
