package de.rwth.idsg.steve.repository;

import de.rwth.idsg.steve.ocpp.OcppProtocol;
import de.rwth.idsg.steve.repository.dto.InsertConnectorStatusParams;
import de.rwth.idsg.steve.repository.dto.InsertTransactionParams;
import de.rwth.idsg.steve.repository.dto.UpdateChargeboxParams;
import de.rwth.idsg.steve.repository.dto.UpdateTransactionParams;
import ocpp.cs._2015._10.MeterValue;
import org.joda.time.DateTime;

import java.util.List;

public interface OcppServerRepository {

    void updateChargebox(UpdateChargeboxParams params);
    void updateOcppProtocol(String chargeBoxId, OcppProtocol protocol);
    void updateEndpointAddress(String chargeBoxIdentity, String endpointAddress);
    void updateChargeboxFirmwareStatus(String chargeBoxIdentity, String firmwareStatus);
    void updateChargeboxDiagnosticsStatus(String chargeBoxIdentity, String status);
    void updateChargeboxHeartbeat(String chargeBoxIdentity, DateTime ts);

    void insertConnectorStatus(InsertConnectorStatusParams params);

    void insertMeterValues(String chargeBoxIdentity, List<MeterValue> list, int connectorId, Integer transactionId);
    void insertMeterValues(String chargeBoxIdentity, List<MeterValue> list, int transactionId);

    int insertTransaction(InsertTransactionParams params);
    void updateTransaction(UpdateTransactionParams params);
}
