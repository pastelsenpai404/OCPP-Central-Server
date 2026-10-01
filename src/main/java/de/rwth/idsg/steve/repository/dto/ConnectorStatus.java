package de.rwth.idsg.steve.repository.dto;

import de.rwth.idsg.steve.ocpp.OcppProtocol;
import lombok.Builder;
import lombok.Getter;
import lombok.Setter;
import org.joda.time.DateTime;

@Getter
@Builder
public final class ConnectorStatus {
    private final String chargeBoxId, timeStamp, status, errorCode;
    private final int chargeBoxPk, connectorId;

    // For additional internal processing. Not related to the humanized
    // String version above, which is for representation on frontend
    private final DateTime statusTimestamp;

    private final OcppProtocol ocppProtocol;

    // This is true, if the chargeBox this connector belongs to is a WS/JSON station
    // and it is disconnected at the moment of building this DTO.
    @Setter
    @Builder.Default
    private boolean jsonAndDisconnected = false;
}
