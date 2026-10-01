package de.rwth.idsg.steve.web.dto.ocpp;

import lombok.Getter;
import lombok.Setter;

import javax.validation.constraints.NotNull;

@Getter
@Setter
public class DataTransferParams extends MultipleChargePointSelect {

    @NotNull(message = "Vendor ID is required")
    private String vendorId;

    private String messageId;

    private String data;
}
