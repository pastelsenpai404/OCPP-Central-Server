package de.rwth.idsg.steve.web.dto.ocpp;

import de.rwth.idsg.steve.repository.dto.ChargePointSelect;
import lombok.Getter;
import lombok.Setter;

import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import java.util.Collections;
import java.util.List;

@Getter
@Setter
public class MultipleChargePointSelect implements ChargePointSelection {

    @NotNull(message = "Charge point selection is required")
    @Size(min = 1, message = "Please select at least {min} charge point")
    private List<ChargePointSelect> chargePointSelectList = Collections.emptyList();
}
