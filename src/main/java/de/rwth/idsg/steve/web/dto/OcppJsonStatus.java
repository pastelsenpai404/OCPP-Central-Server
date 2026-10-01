package de.rwth.idsg.steve.web.dto;

import de.rwth.idsg.steve.ocpp.OcppVersion;
import lombok.Builder;
import lombok.Getter;
import lombok.ToString;
import org.joda.time.DateTime;

@Getter
@Builder
@ToString
public final class OcppJsonStatus {
    private final int chargeBoxPk;
    private final String chargeBoxId, connectedSince;
    private final String connectionDuration;
    private final OcppVersion version;
    private final DateTime connectedSinceDT;
}
