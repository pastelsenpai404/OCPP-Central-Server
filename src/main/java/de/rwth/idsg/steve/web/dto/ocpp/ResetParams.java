package de.rwth.idsg.steve.web.dto.ocpp;

import lombok.Getter;
import lombok.Setter;

import javax.validation.constraints.NotNull;

@Getter
@Setter
public class ResetParams extends MultipleChargePointSelect {

    @NotNull(message = "Reset Type is required")
    private ResetType resetType;
}
