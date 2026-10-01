package de.rwth.idsg.steve.repository.dto;

import lombok.Builder;
import lombok.Getter;

@Getter
@Builder
public class ChargingProfileAssignment {
    private final int chargeBoxPk, connectorId, chargingProfilePk;
    private final String chargeBoxId, chargingProfileDescription;
}
