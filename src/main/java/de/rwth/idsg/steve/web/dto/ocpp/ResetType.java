package de.rwth.idsg.steve.web.dto.ocpp;

public enum ResetType {
    HARD("Hard"),
    SOFT("Soft");

    private final String value;

    ResetType(String v) {
        value = v;
    }

    public String value() {
        return value;
    }

    public static ResetType fromValue(String v) {
        for (ResetType c : ResetType.values()) {
            if (c.value.equals(v)) {
                return c;
            }
        }
        throw new IllegalArgumentException(v);
    }
}
