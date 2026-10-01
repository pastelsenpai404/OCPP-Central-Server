package de.rwth.idsg.steve;

public enum ApplicationProfile {
    DEV,
    TEST,
    PROD;

    public static ApplicationProfile fromName(String v) {
        for (ApplicationProfile ap : ApplicationProfile.values()) {
            if (ap.name().equalsIgnoreCase(v)) {
                return ap;
            }
        }
        throw new IllegalArgumentException(v);
    }

    public boolean isProd() {
        return this == PROD;
    }
}
