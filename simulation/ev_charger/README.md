# EV charger simulation

C++20 backend + SvelteKit/TypeScript static frontend + C++ WebAssembly estimate.
Runs on Windows and Linux. One virtual connector per station; maximum four stations.
All charger state is authoritative on the backend. No billing database or real payment access.

## Local Windows

From the repository root:

```powershell
.\simulation\ev_charger\run.ps1
# On later runs, after a successful build:
.\simulation\ev_charger\run.ps1 -SkipBuild
```

Requires Visual Studio 2022 C++ Build Tools + CMake, Node 22.17+ and Emscripten.
Scripts also discover the existing billing_management/.deps/emsdk toolchain when installed.
Alternatively activate your own Emscripten SDK before running.
Open http://127.0.0.1:5600/. The first run creates ignored `config.local.json` with a random
`SIM_ADMIN_TOKEN`; copy that value into the login screen. Keep the PowerShell window open.
The token is never printed, committed, stored by the browser, or shipped in the frontend.
Stop with Ctrl+C. Restart clears stations, sessions, meter counters and protocol logs.

Start in **Mock CSMS**: add SIM01, select Normal session. TEST001 is accepted;
tags beginning DENY are rejected. Plug/unplug, pause/resume, stop, network loss/reconnect,
availability, fault injection, soft reset, grid limits and 1×/10×/60× virtual time are available.
Trace shows actual backend-generated OCPP frames and responses, not prerecorded animations.

## Real CSMS

Set the server-side `SIM_CSMS_URL` prefix, for example
`ws://127.0.0.1:5003/ocpp/` locally or `wss://ocpp.barryofeverything.com/ocpp/` remotely.
Plain WS is restricted to loopback. WSS validates certificates and hostnames.
Windows uses the OS certificate store; Linux supports optional SIM_CA_FILE.
Set `SIM_STATION_SECRETS` to an object mapping SIM01 to its separate 64-hex station password.
Then choose **Real CSMS** when adding that configured station.

Provision the same station ID and secret on the CSMS first (station_secrets config plus
the CSMS admin provisioning API), and use an authorized test RFID tag. The simulator
does not change the CSMS configuration, restart it or reuse a real station's identity.
Connecting real mode generates real OCPP transactions on that CSMS: use its test billing mode.

## Supported OCPP subset

| Operation | 1.6 JSON | 2.0.1 JSON |
|---|---|---|
| Boot / heartbeat / connector status / authorization | Yes | Yes |
| Start / meter / stop | StartTransaction, MeterValues, StopTransaction | TransactionEvent Started, Updated, Ended |
| Remote session control | RemoteStart/StopTransaction | RequestStart/StopTransaction |
| Reset, availability, unlock, trigger | Basic support | Basic support |
| Charging profile | One immediate absolute W limit, no dates/duration | Same restricted subset |
| Unsupported commands | CALLERROR NotImplemented | CALLERROR NotImplemented |

Offline samples/end events use a bounded in-memory queue (128 per station). Pending calls
expire after 30 seconds. A lost start acknowledgement marks the station uncertain and blocks
another start: inspect the CSMS transaction before deleting/recreating it. Outbound calls
are not durable across process restarts and ambiguous failed sends are not automatically replayed.
This is a protocol/operational lab, not hardware firmware or a complete OCPP certification suite.
The UI requires an explicit reconciliation checkbox before removing an uncertain station.

The simplified energy model includes 92% efficiency, EV power acceptance, grid constraints,
SoC taper above 80% and a temperature-based power factor. Virtual time accelerates physics;
protocol timestamps, heartbeat and meter intervals use wall-clock time. WASM computes an
ideal estimate only; backend integration accounts for limits/taper. No electrical safety claim.

## Structure

```
backend/include/simulation/     application and transport contracts
backend/src/application/       station state, lifecycle, bounded queues
backend/src/protocol/          OCPP message generation
backend/src/transport/         Linux OpenSSL / Windows WinHTTP WebSocket adapters
backend/src/interfaces/http/   authenticated HTTP API and local static files
shared/domain/                 pure C++ battery physics
shared/wasm/                   browser export of estimate calculation
frontend/src/lib/              typed API and WASM clients
frontend/src/routes/           lab UI and styles
tests/unit/                    physics invariants
tests/integration/             isolated HTTP/OCPP workflow tests
scripts/                       local builds and toolchain discovery
deploy/                        Linux lifecycle, Nginx, systemd, config and smoke checks
```

## Deployment and DNS

In DigitalOcean Networking → Domains → barryofeverything.com → Create new record:
type **A**, hostname **ev.simulation**, target **104.248.96.73**, TTL **300**.
Remove conflicting A/AAAA entries for that hostname. After DNS resolves:

```powershell
.\deploy.ps1 -Target Simulation
# Before DNS is ready, build/start loopback API and stage HTTP challenge site:
.\deploy.ps1 -Target Simulation -PrepareOnly
# Create only a source/static archive, with no SSH:
.\deploy.ps1 -Target Simulation -PackageOnly
```

Root deploy All now includes OCPP, billing and this simulator. HTTPS is
https://ev.simulation.barryofeverything.com/. API stays on 127.0.0.1:5600 behind Nginx.
Deployment preserves `/etc/ev-simulation/runtime.env` credentials; its SIM_ADMIN_TOKEN
is separate from CSMS/billing tokens. Read it privately over your SSH terminal for login.
File is root-only 0600. Edit this file to configure real-mode station secrets, then
`systemctl restart ev-simulation`. Systemd JSON values need outer single quotes, e.g.
`SIM_STATION_SECRETS='{"SIM01":"...64 hex..."}'`.

The deploy script pins the existing PuTTY server host key, checks port ownership and DNS,
builds/tests C++ on Linux, uses an unprivileged service, atomically switches releases,
smoke-checks auth/readiness and restores the previous runtime/config/Nginx on failure.
Certbot installs/renews a certificate for this hostname. In PrepareOnly mode a new public
HTTP site returns 503 until HTTPS deployment succeeds. It does not expose the lab over HTTP.
Do not use an active lab during deployment: all state is in memory and a restart clears it.

## Validation

```powershell
.\simulation\ev_charger\scripts\build-native.ps1
.\simulation\ev_charger\scripts\build-frontend.ps1
py -3 -m pip install -r simulation/ev_charger/tests/requirements.txt
py -3 simulation/ev_charger/tests/integration/workflows.py simulation/ev_charger/build/native/backend/Release/ev_charger_server.exe
```

Linux: cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release, then
cmake --build build/native --parallel 1 and ctest --test-dir build/native --output-on-failure.
Requires OpenSSL development headers. Set environment values from runtime.env.example
before running build/native/backend/ev_charger_server.

For browser checks with installed Microsoft Edge, install tests/requirements-browser.txt and
run tests/integration/browser.py with the server executable path. Linux workflow tests can set
SIM_TEST_SCHEMAS to the checked-in ocpp_csms/schemas directory; missing schemas fail the test.
