# Integration contracts

Versioned HTTP/event contracts belong here when server adapters are implemented.
Management and customer APIs have separate authorization boundaries. Customer identity
must come from an authenticated server session, never a customer ID trusted from the UI.
CSMS charging facts must carry a stable source ID for replay-safe invoice processing.

The initial HTTP API is limited to readiness and authenticated sandbox quote previews
(`GET /health/ready`, `POST /api/v1/quote`). It does not implement customer account
authentication, read the CSMS database, persist invoices or execute payments.
