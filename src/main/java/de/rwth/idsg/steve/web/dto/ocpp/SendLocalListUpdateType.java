package de.rwth.idsg.steve.web.dto.ocpp;

public enum SendLocalListUpdateType {
    DIFFERENTIAL("Differential"),
    FULL("Full");

    private final String value;

    SendLocalListUpdateType(String v) {
        value = v;
    }

    public String value() {
        return value;
    }

    public static SendLocalListUpdateType fromValue(String v) {
        for (SendLocalListUpdateType c : SendLocalListUpdateType.values()) {
            if (c.value.equals(v)) {
                return c;
            }
        }
        throw new IllegalArgumentException(v);
    }
}
