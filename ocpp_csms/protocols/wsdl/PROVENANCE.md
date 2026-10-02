# Legacy WSDL provenance

The six WSDL files are unmodified originals from `RWTH-i5-IDSG/ocpp-jaxb`, tag `0.0.3`, commit `0fed974f40329ca86f7301974735c705cbe4c9b8`, matching the original Java dependency. See the accompanying `LICENSE-ocpp-jaxb.txt`.

`scripts/generate-legacy-schemas.py` generates JSON validation artifacts and `soap.registry` typed XML metadata. Generated sets contain 36, 48 and 56 request/response definitions for OCPP 1.2, 1.5 and 1.6. Resource bounds are intentionally added to strings and arrays; the originals remain unchanged. These generated WSDL artifacts are separate from the upstream OCPP 1.6 JSON schemas and the 2.0.1 schemas.
