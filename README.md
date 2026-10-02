# EV charging workspace

- [ocpp_csms](ocpp_csms/README.md): existing C++ OCPP server, Control room and deployment scripts (formerly `cpp/`).
- [billing_management](billing_management/README.md): independent C++ billing foundation, with management and customer applications and WebAssembly previews.
- `material/`: private local infrastructure files; ignored by Git.

The two products have independent builds and configuration. Billing must integrate with CSMS through explicit contracts rather than including its source or accessing its private credentials. Existing Linux service names and `/opt/ocpp-cpp` paths remain unchanged by the repository folder rename.

Run `./deploy.ps1` from this directory to deploy CSMS and all four billing sites to
`104.248.96.73`. Use `-Target Billing` or `-Target Ocpp` to deploy one product,
`-PrepareOnly` while DNS is pending, or `-PackageOnly` to build/package locally.
See [billing deployment and DNS records](billing_management/docs/DEPLOYMENT.md).
