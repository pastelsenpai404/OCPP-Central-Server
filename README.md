# EV charging workspace

- [ocpp_csms](ocpp_csms/README.md): existing C++ OCPP server, Control room and deployment scripts (formerly `cpp/`).
- [billing_management](billing_management/README.md): independent C++ billing foundation, with management and customer applications and WebAssembly previews.
- `material/`: private local infrastructure files; ignored by Git.

The two products have independent builds and configuration. Billing must integrate with CSMS through explicit contracts rather than including its source or accessing its private credentials. Existing Linux service names and `/opt/ocpp-cpp` paths remain unchanged by the repository folder rename.
