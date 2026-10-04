# EV charging workspace

- [ocpp_csms](ocpp_csms/README.md): existing C++ OCPP server, Control room and deployment scripts (formerly `cpp/`).
- [billing_management](billing_management/README.md): independent C++ billing foundation, with management and customer applications and WebAssembly previews.
- [simulation/ev_charger](simulation/ev_charger/README.md): C++ virtual EV chargers, SvelteKit/WASM lab, OCPP 1.6/2.0.1 mock and real WebSocket modes.
- `material/`: private local infrastructure files; ignored by Git.

The products have independent builds and configuration. Billing and simulation integrate with CSMS through explicit contracts rather than including its source or accessing its private credentials. Existing Linux service names and `/opt/ocpp-cpp` paths remain unchanged by the repository folder rename.

Run `./deploy.ps1` from this directory to deploy CSMS, all four billing sites and the simulator to
`104.248.96.73`. Use `-Target Billing`, `-Target Ocpp` or `-Target Simulation` to deploy one product,
`-PrepareOnly` while DNS is pending, or `-PackageOnly` to build/package locally.
See [billing deployment and DNS records](billing_management/docs/DEPLOYMENT.md).
Simulator DNS: A `ev.simulation` → `104.248.96.73`; see its README for local run/token setup.
