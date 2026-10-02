# Integration contracts

Versioned HTTP/event contracts belong here when server adapters are implemented.
Management and customer APIs have separate authorization boundaries. Customer identity
must come from an authenticated server session, never a customer ID trusted from the UI.
CSMS charging facts must carry a stable source ID for replay-safe invoice processing.

No network API is implemented by this scaffold. The quote CLI and WASM preview are
local demonstrations; they neither read the CSMS database nor persist invoices.
