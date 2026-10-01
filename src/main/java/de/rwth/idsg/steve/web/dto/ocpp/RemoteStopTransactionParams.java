package de.rwth.idsg.steve.web.dto.ocpp;

import lombok.Getter;
import lombok.Setter;

import javax.validation.constraints.NotNull;

@Getter
@Setter
public class RemoteStopTransactionParams extends SingleChargePointSelect {

    @NotNull(message = "Transaction ID is required")
    private Integer transactionId;
}
