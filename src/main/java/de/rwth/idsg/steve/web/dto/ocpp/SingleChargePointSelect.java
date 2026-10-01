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
public class SingleChargePointSelect implements ChargePointSelection {

    @NotNull(message = "Charge point selection is required")
    @Size(min = 1, max = 1, message = "It is required to select exactly 1 charge point")
    private List<ChargePointSelect> chargePointSelectList = Collections.emptyList();
}
