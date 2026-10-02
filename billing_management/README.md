# Billing management

C++20 billing foundation for two separate applications, with SvelteKit 3 / Svelte 5 / TypeScript frontends and C++ WebAssembly. **Current scope: local sandbox quote CLI backends and browser previews.** There are no HTTP APIs, customer login, invoice persistence, taxes, payment gateways or production billing yet. Existing CSMS billing and production databases are not changed.

```text
billing_management/
  management/
    backend/                 # Staff application: domain, application, infrastructure, interfaces
    frontend/                # Staff SvelteKit app and WASM build target
  customer/
    backend/                 # Customer application, independent executable
    frontend/                # Customer SvelteKit app and WASM build target
  shared/
    domain/                  # Pure C++ money/energy arithmetic
    contracts/               # Future versioned integration schemas
    frontend/                # @billing/ui: Svelte components, styles and typed WASM boundary
  cmake/                     # Build helpers
  scripts/                   # Local build entrypoints
  tests/unit/                # Shared domain tests
  docs/                      # Architecture and contribution rules
```

The folder name is consistently `management` (rather than `mangement`). Backend executables are separate from each other and from `../ocpp_csms`. No dependency is introduced on CSMS build directories or private configuration.

## Native backends: Windows and Linux

Windows: Visual Studio 2022 C++ Build Tools, including CMake.

```powershell
./scripts/build-native.ps1
./build/native/management/backend/Release/billing_management_backend.exe 1500 750
./build/native/customer/backend/Release/billing_customer_backend.exe 1500 750
```

Linux: C++20 compiler and CMake 3.24+.

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
./build/native/management/backend/billing_management_backend 1500 750
```

Both commands emit a sandbox subtotal of `1125` satang for 1500 Wh at 750 satang/kWh. Invalid inputs return exit code 2. The CLI is an initial application adapter; it is not a running HTTP server.

## SvelteKit + WebAssembly frontends

Requirements: Node.js 22.17+ (Node 24 LTS recommended), npm and the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html). SDK/build/dependency directories are ignored. C++ domain rules compile into `.wasm`; SvelteKit handles routes, components, TypeScript and rendering. Both apps use [adapter-static](https://svelte.dev/docs/kit/adapter-static), with rendered HTML and browser hydration. WASM loads only inside `onMount`, never during SSR or prerendering.

Windows, from `billing_management` (the helper also finds Node in the local SDK if your system Node is too old):

```powershell
./scripts/run-frontend.ps1 -Area management
# In another terminal:
./scripts/run-frontend.ps1 -Area customer
```

Open `http://127.0.0.1:5100` for management and `http://127.0.0.1:5101` for customer. Use `-BuildWasm` after changing C++ quote code. On a fresh checkout, install/activate the SDK before using the helper.

Manual workflow (Windows: `./scripts/build-wasm.ps1`; Linux: the two `emcmake` commands below):

```sh
emcmake cmake -S . -B build/wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --parallel 2
npm ci
npm run wasm:sync
npm run dev:management
# or npm run dev:customer
```

Run `npm run check` for Svelte/TypeScript diagnostics, then `npm run build` for both production frontends. Output: `management/frontend/dist/` and `customer/frontend/dist/`; deploy these directories as separate static sites (for example, with Nginx). Backend executables remain C++; no Node server is required for the static output. The root `package-lock.json` pins dependencies for all npm workspaces.

Each frontend follows the SvelteKit structure:

```text
frontend/
  src/app.html               # Document shell
  src/routes/+layout.ts      # Prerender options
  src/routes/+layout.svelte  # Global styles/layout
  src/routes/+page.svelte    # Application page
  src/lib/wasm/quote.ts      # Application WASM facade
  static/wasm/               # Generated ignored JS/WASM assets
  vite.config.ts             # SvelteKit and static adapter configuration
```

Shared UI lives in `shared/frontend/components/`; the typed loader is in `shared/frontend/wasm/quote.ts`. CMake produces only JS/WASM artifacts under `build/wasm`; `npm run wasm:sync` copies exactly those two generated files into each app's static directory. Frontend build never copies native executables or private configuration.

After building, run `node tests/wasm/quote-tests.mjs` with Node.js 22+ to check both compiled modules against the native arithmetic cases. The module loader uses the browser build with supplied WASM bytes; these checks do not require a backend or credentials.

The preview does not contact a backend. All authoritative billing must be recalculated and authorized server-side; browser WASM is untrusted. Use integer Wh and satang, with explicitly documented rounding. Do not add database credentials, payment secrets or API admin tokens to frontend files.

Read [architecture](docs/ARCHITECTURE.md) and [contribution rules](CONTRIBUTING.md) before extending this foundation.
