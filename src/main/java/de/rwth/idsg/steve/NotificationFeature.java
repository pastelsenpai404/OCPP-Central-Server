package de.rwth.idsg.steve;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

@RequiredArgsConstructor
public enum NotificationFeature {

    // Ocpp related
    //
    OcppStationBooted(" a charging station sends a boot notification (Note: This activates notifications about failed connection attempts for unregistered JSON stations, as well)"),
    OcppStationStatusFailure(" a connector gets faulted"),
    OcppStationWebSocketConnected(" a JSON charging station connects"),
    OcppStationWebSocketDisconnected(" a JSON charging station disconnects"),
    OcppTransactionStarted(" a charging station starts a transaction"),
    OcppTransactionEnded(" a charging station ends a transaction");


    @Getter
    private final String text;

    public static NotificationFeature fromName(String v) {
        for (NotificationFeature c: NotificationFeature.values()) {
            if (c.name().equalsIgnoreCase(v)) {
                return c;
            }
        }
        throw new IllegalArgumentException(v);
    }
}
