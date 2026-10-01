package de.rwth.idsg.steve.web.dto.ocpp;

public enum AvailabilityType {
    INOPERATIVE("Inoperative"),
    OPERATIVE("Operative");

    private final String value;

    AvailabilityType(String v) {
        value = v;
    }

    public String value() {
        return value;
    }

    public static AvailabilityType fromValue(String v) {
        for (AvailabilityType c : AvailabilityType.values()) {
            if (c.value.equals(v)) {
                return c;
            }
        }
        throw new IllegalArgumentException(v);
    }
}
