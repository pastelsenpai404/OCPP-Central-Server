package de.rwth.idsg.steve.ocpp.ws.data;

public abstract class OcppJsonResponse extends OcppJsonMessage {
    public OcppJsonResponse(MessageType messageType) {
        super(messageType);
    }
}
