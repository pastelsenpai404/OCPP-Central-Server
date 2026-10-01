package de.rwth.idsg.steve.web.dto;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

@RequiredArgsConstructor
public enum UserSex {
    FEMALE("f", "Female"),
    MALE("m", "Male"),
    OTHER("o", "Other");

    @Getter private final String databaseValue;
    @Getter private final String value;

    public static UserSex fromDatabaseValue(String v) {
        for (UserSex c: UserSex.values()) {
            if (c.databaseValue.equals(v)) {
                return c;
            }
        }
        throw new IllegalArgumentException(v);
    }

    public static UserSex fromValue(String v) {
        for (UserSex c: UserSex.values()) {
            if (c.value.equals(v)) {
                return c;
            }
        }
        throw new IllegalArgumentException(v);
    }
}
