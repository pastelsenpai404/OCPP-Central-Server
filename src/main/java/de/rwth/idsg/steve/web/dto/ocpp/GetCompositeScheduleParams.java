package de.rwth.idsg.steve.web.dto.ocpp;

import lombok.Getter;
import lombok.Setter;
import ocpp.cp._2015._10.ChargingRateUnitType;

import javax.validation.constraints.Min;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Positive;

@Getter
@Setter
public class GetCompositeScheduleParams extends MultipleChargePointSelect {

    @NotNull
    @Min(value = 0, message = "Connector ID must be at least {value}")
    private Integer connectorId;

    @NotNull
    @Positive
    private Integer durationInSeconds;

    private ChargingRateUnitType chargingRateUnit;

}
