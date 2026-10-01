package de.rwth.idsg.steve.repository.dto;

import lombok.Getter;
import ocpp.cs._2015._10.ChargePointErrorCode;
import ocpp.cs._2015._10.ChargePointStatus;

@Getter
public enum TransactionStatusUpdate {

    AfterStart(ChargePointStatus.CHARGING),
    AfterStop(ChargePointStatus.AVAILABLE);

    private final String status;
    private final String errorCode = ChargePointErrorCode.NO_ERROR.value();

    TransactionStatusUpdate(ChargePointStatus status) {
        this.status = status.value();
    }
}
