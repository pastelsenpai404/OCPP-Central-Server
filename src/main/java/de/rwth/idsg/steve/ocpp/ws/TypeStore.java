package de.rwth.idsg.steve.ocpp.ws;

import de.rwth.idsg.ocpp.jaxb.RequestType;
import de.rwth.idsg.steve.ocpp.ws.data.ActionResponsePair;

public interface TypeStore {

    /**
     * For incoming requests
     *
     * Action field --> Request JAXB class
     */
    Class<? extends RequestType> findRequestClass(String action);

    /**
     * For outgoing requests
     *
     * Request JAXB class --> Action field, Response JAXB class
     */
    <T extends RequestType> ActionResponsePair findActionResponse(T requestPayload);
}
