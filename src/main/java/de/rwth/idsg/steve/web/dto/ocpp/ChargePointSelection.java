package de.rwth.idsg.steve.web.dto.ocpp;

import de.rwth.idsg.steve.repository.dto.ChargePointSelect;

import java.util.List;

public interface ChargePointSelection {
    List<ChargePointSelect> getChargePointSelectList();
}
