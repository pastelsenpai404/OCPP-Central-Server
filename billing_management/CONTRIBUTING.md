# Team development

1. Keep each feature within its owning management/customer application. Share a rule only when both applications use identical semantics.
2. Use namespaces `billing::domain`, `billing::management::application` and `billing::customer::application`. Keep public headers under matching `include/billing/...` paths.
3. Keep controllers/adapters thin. Put orchestration in application and arithmetic/state rules in domain. Inject infrastructure through application interfaces.
4. Add CMake targets for modules; use target include directories and compile options rather than global flags or relative includes into another product.
5. Run native CTest for domain/use-case changes. Add meaningful edge, rounding and overflow cases for monetary logic. Build both WASM applications when changing shared domain or frontend code.
6. Treat frontend input as untrusted. No authentication decisions or authoritative invoice totals may rely solely on browser code.
7. Add `.example` templates alongside future private configuration, containing placeholders only. Keep real credentials ignored. Avoid committing build outputs, caches or logs.
8. Document new contracts, migration compatibility and remaining limitations. A local preview must never be described as a production payment integration.
9. Use SvelteKit routes for application pages and `src/lib` for application code. Share UI through `@billing/ui`, with typed props and accessible form controls. Run `npm run check` and `npm run build` after frontend changes; commit the root npm lockfile. Load WASM only in the browser lifecycle and keep generated assets ignored.
