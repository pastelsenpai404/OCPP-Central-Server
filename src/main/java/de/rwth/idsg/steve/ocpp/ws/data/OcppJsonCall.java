package de.rwth.idsg.steve.ocpp.ws.data;

import de.rwth.idsg.ocpp.jaxb.RequestType;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class OcppJsonCall extends OcppJsonMessage {
    private String action;
    private RequestType payload;

    public OcppJsonCall() {
        super(MessageType.CALL);
    }
}
