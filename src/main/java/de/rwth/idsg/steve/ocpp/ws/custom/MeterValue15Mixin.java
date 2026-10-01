package de.rwth.idsg.steve.ocpp.ws.custom;

import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import ocpp.cs._2012._06.MeterValue;

import java.util.List;

public abstract class MeterValue15Mixin {

    @JsonDeserialize(using = MeterValue15Deserializer.class)
    List<MeterValue> values;
}
