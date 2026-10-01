package de.rwth.idsg.steve.ocpp.ws.custom;

import com.fasterxml.jackson.annotation.JsonValue;

public interface EnumMixin {

    @JsonValue
    String value();
}
