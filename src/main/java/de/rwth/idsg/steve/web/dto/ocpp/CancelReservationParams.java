package de.rwth.idsg.steve.web.dto.ocpp;

import lombok.Getter;
import lombok.Setter;

import javax.validation.constraints.Min;
import javax.validation.constraints.NotNull;

@Getter
@Setter
public class CancelReservationParams extends SingleChargePointSelect {

    @NotNull(message = "Reservation ID is required")
    @Min(value = 0, message = "Reservation ID must be at least {value}")
    private Integer reservationId;
}
