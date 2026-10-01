package de.rwth.idsg.steve.repository.dto;

import de.rwth.idsg.steve.ocpp.OcppTransport;
import lombok.Getter;
import lombok.RequiredArgsConstructor;

@RequiredArgsConstructor
@Getter
public final class ChargePointSelect {
    private final OcppTransport ocppTransport;
    private final String chargeBoxId;
    private final String endpointAddress;

    public ChargePointSelect(OcppTransport ocppTransport, String chargeBoxId) {
        // Provide a non-null value (or placeholder if you will) to frontend for JSON charge points.
        // This is clearly a hack. Not my proudest moment.
        this(ocppTransport, chargeBoxId, "-");
    }

    public boolean isEndpointAddressSet() {
        return !("-".equals(endpointAddress));
    }

    public boolean isSoap() {
        return OcppTransport.SOAP == ocppTransport;
    }
}
