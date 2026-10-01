package de.rwth.idsg.steve.ocpp.ws.data;

import de.rwth.idsg.ocpp.jaxb.ResponseType;
import de.rwth.idsg.steve.ocpp.CommunicationTask;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.Setter;

@Getter
@Setter
@RequiredArgsConstructor
public class FutureResponseContext {
    private final CommunicationTask task;
    private final Class<? extends ResponseType> responseClass;
}
