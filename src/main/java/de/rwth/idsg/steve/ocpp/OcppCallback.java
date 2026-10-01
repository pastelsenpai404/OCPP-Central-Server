package de.rwth.idsg.steve.ocpp;

import de.rwth.idsg.steve.ocpp.ws.data.OcppJsonError;

public interface OcppCallback<T> {

    void success(String chargeBoxId, T response);

    /**
     * Relevant to WebSocket/JSON transport: Even though we have an error, this object is still a valid response from
     * charge point and the implementation should treat it as such. {@link CommunicationTask#addNewError(String, String)}
     * should be used when the request could not be delivered and there is a Java exception.
     */
    void success(String chargeBoxId, OcppJsonError error);

    // -------------------------------------------------------------------------
    // Technical errors ((e.g. communication problems)
    // -------------------------------------------------------------------------

    void failed(String chargeBoxId, Exception e);

}
