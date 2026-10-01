OCPP 1.6 and security extension JSON schemas mirrored by mobilityhouse/ocpp.

Source: https://github.com/mobilityhouse/ocpp/tree/f92059f954fc10c4e3524c5f6d274c847457ccc3/ocpp/v16/schemas

Retrieved 2026-10-02. Original files retained unmodified. Upstream license is included.
78 files cover 39 request/response pairs; shipping schemas does not mean shipping handlers for every extension.
The validator removes the draft-04 marker and legacy `id` in memory; these files use constraints compatible with draft-07 and no exclusive boolean bounds or external refs.
Confirm these schemas against the OCA package used for certification before release.
