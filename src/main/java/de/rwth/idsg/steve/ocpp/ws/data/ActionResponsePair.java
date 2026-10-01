package de.rwth.idsg.steve.ocpp.ws.data;

import de.rwth.idsg.ocpp.jaxb.ResponseType;
import lombok.Getter;
import lombok.RequiredArgsConstructor;

@Getter
@RequiredArgsConstructor
public final class ActionResponsePair {
    private final String action;
    private final Class<? extends ResponseType> responseClass;
}
