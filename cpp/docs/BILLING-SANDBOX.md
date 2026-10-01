# Billing sandbox

The isolated simulator persists cases in `cpp_billing_sandbox`. It does not contact payment providers, move money, issue receipts or send commands to physical chargers. It prepares arithmetic tests before migration of the legacy billing workflows.

Use the admin bearer token with `POST /api/v1/billing/sandbox/settle`:

```json
{
  "caseId": "SIMULATION_01",
  "currency": "THB",
  "paidMinor": 1000,
  "pricePerKwhMinor": 800,
  "parkingPerMinuteMinor": 20,
  "parkingGraceSeconds": 60,
  "meterStart": {"value": "10.000000", "unit": "kWh"},
  "meterStop": {"value": "11000", "unit": "Wh"},
  "chargeEndedAt": "2026-10-02T01:00:00Z",
  "departedAt": "2026-10-02T01:01:00.000001Z"
}
```

`1000` minor THB units means 10 baht. Rates are explicit test inputs. Decimal readings normalize exactly to integer milliWh, accepting three decimal places for Wh or six for kWh. Unsupported precision, overflow, meter rollback, reversed timestamps and parking periods above 366 days require reconciliation.

Energy cost rounds upward to one minor currency unit. Parking bills each started minute after grace; one microsecond beyond grace starts its first billable minute. UTC offsets are normalized. The example reports energy cost 800, parking cost 20 and simulated refund 180, in minor units. Every response includes `"mode":"sandbox"` and `"paymentExecuted":false`.

Repeating a case ID with identical JSON returns the same stored result; different inputs are rejected. Administrators can read the paginated ledger at `GET /api/v1/billingSandbox`. Tax, provider fees, company tariffs, meter rollovers and legacy receipt/parking rules are outside this simulator.
