package de.rwth.idsg.steve.ocpp.ws.data;

import de.rwth.idsg.ocpp.jaxb.ResponseType;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class OcppJsonResult extends OcppJsonResponse {
    private ResponseType payload;

    public OcppJsonResult() {
        super(MessageType.CALL_RESULT);
    }
}
