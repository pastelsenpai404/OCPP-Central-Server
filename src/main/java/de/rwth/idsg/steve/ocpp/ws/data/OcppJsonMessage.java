package de.rwth.idsg.steve.ocpp.ws.data;

import lombok.Getter;
import lombok.Setter;

/**
 * Class hierarchy:
 *
 *      OcppJsonMessage
 *      +
 *      +--> OcppJsonCall
 *      +
 *      +--> OcppJsonResponse
 *           +
 *           +--> OcppJsonResult
 *           +
 *           +--> OcppJsonError
 */
@Getter
@Setter
public abstract class OcppJsonMessage {
    private final MessageType messageType;
    private String messageId;

    public OcppJsonMessage(MessageType messageType) {
        this.messageType = messageType;
    }
}
