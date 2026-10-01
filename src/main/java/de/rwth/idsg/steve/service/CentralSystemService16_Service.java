package de.rwth.idsg.steve.service;

import de.rwth.idsg.steve.ocpp.OcppProtocol;
import de.rwth.idsg.steve.repository.OcppServerRepository;
import de.rwth.idsg.steve.repository.SettingsRepository;
import de.rwth.idsg.steve.repository.dto.InsertConnectorStatusParams;
import de.rwth.idsg.steve.repository.dto.InsertTransactionParams;
import de.rwth.idsg.steve.repository.dto.UpdateChargeboxParams;
import de.rwth.idsg.steve.repository.dto.UpdateTransactionParams;
import de.rwth.idsg.steve.service.notification.OccpStationBooted;
import de.rwth.idsg.steve.service.notification.OcppStationStatusFailure;
import de.rwth.idsg.steve.service.notification.OcppTransactionEnded;
import de.rwth.idsg.steve.service.notification.OcppTransactionStarted;
import de.rwth.idsg.steve.utils.OutboundHttp;
import jooq.steve.db.enums.TransactionStopEventActor;
import lombok.extern.slf4j.Slf4j;
import ocpp.cs._2015._10.AuthorizationStatus;
import ocpp.cs._2015._10.AuthorizeRequest;
import ocpp.cs._2015._10.AuthorizeResponse;
import ocpp.cs._2015._10.BootNotificationRequest;
import ocpp.cs._2015._10.BootNotificationResponse;
import ocpp.cs._2015._10.ChargePointStatus;
import ocpp.cs._2015._10.DataTransferRequest;
import ocpp.cs._2015._10.DataTransferResponse;
import ocpp.cs._2015._10.DataTransferStatus;
import ocpp.cs._2015._10.DiagnosticsStatusNotificationRequest;
import ocpp.cs._2015._10.DiagnosticsStatusNotificationResponse;
import ocpp.cs._2015._10.FirmwareStatusNotificationRequest;
import ocpp.cs._2015._10.FirmwareStatusNotificationResponse;
import ocpp.cs._2015._10.HeartbeatRequest;
import ocpp.cs._2015._10.HeartbeatResponse;
import ocpp.cs._2015._10.IdTagInfo;
import ocpp.cs._2015._10.MeterValuesRequest;
import ocpp.cs._2015._10.MeterValuesResponse;
import ocpp.cs._2015._10.RegistrationStatus;
import ocpp.cs._2015._10.StartTransactionRequest;
import ocpp.cs._2015._10.StartTransactionResponse;
import ocpp.cs._2015._10.StatusNotificationRequest;
import ocpp.cs._2015._10.StatusNotificationResponse;
import ocpp.cs._2015._10.StopTransactionRequest;
import ocpp.cs._2015._10.StopTransactionResponse;
import org.joda.time.DateTime;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.ApplicationEventPublisher;
import org.springframework.stereotype.Service;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Optional;
import java.sql.Timestamp;

import static de.rwth.idsg.steve.SteveConfiguration.CONFIG;

@Slf4j
@Service
public class CentralSystemService16_Service {

    @Autowired
    private OcppServerRepository ocppServerRepository;
    @Autowired
    private SettingsRepository settingsRepository;

    @Autowired
    private OcppTagService ocppTagService;
    @Autowired
    private ApplicationEventPublisher applicationEventPublisher;
    @Autowired
    private ChargePointHelperService chargePointHelperService;

    private static final String BILLING_API_BASE_URL = CONFIG.getBillingApi().getBaseUrl();
    private static final String BILLING_DB_URL = CONFIG.getBillingDb().getUrl();
    private static final String BILLING_DB_USER = CONFIG.getBillingDb().getUserName();
    private static final String BILLING_DB_PASSWORD = CONFIG.getBillingDb().getPassword();

    public BootNotificationResponse bootNotification(BootNotificationRequest parameters, String chargeBoxIdentity,
            OcppProtocol ocppProtocol) {

        Optional<RegistrationStatus> status = chargePointHelperService.getRegistrationStatus(chargeBoxIdentity);
        applicationEventPublisher.publishEvent(new OccpStationBooted(chargeBoxIdentity, status));
        DateTime now = DateTime.now();

        if (status.isEmpty()) {
            // Applies only to stations not in db (regardless of the registration_status
            // field from db)
            log.error("The chargebox '{}' is NOT in database.", chargeBoxIdentity);
        } else {
            // Applies to all stations in db (even with registration_status Rejected)
            log.info("The boot of the chargebox '{}' with registration status '{}' is acknowledged.", chargeBoxIdentity,
                    status);
            UpdateChargeboxParams params = UpdateChargeboxParams.builder()
                    .ocppProtocol(ocppProtocol)
                    .vendor(parameters.getChargePointVendor())
                    .model(parameters.getChargePointModel())
                    .pointSerial(parameters.getChargePointSerialNumber())
                    .boxSerial(parameters.getChargeBoxSerialNumber())
                    .fwVersion(parameters.getFirmwareVersion())
                    .iccid(parameters.getIccid())
                    .imsi(parameters.getImsi())
                    .meterType(parameters.getMeterType())
                    .meterSerial(parameters.getMeterSerialNumber())
                    .chargeBoxId(chargeBoxIdentity)
                    .heartbeatTimestamp(now)
                    .build();

            ocppServerRepository.updateChargebox(params);
        }

        return new BootNotificationResponse()
                .withStatus(status.orElse(RegistrationStatus.REJECTED))
                .withCurrentTime(now)
                .withInterval(settingsRepository.getHeartbeatIntervalInSeconds());
    }

    public FirmwareStatusNotificationResponse firmwareStatusNotification(
            FirmwareStatusNotificationRequest parameters, String chargeBoxIdentity) {
        String status = parameters.getStatus().value();
        ocppServerRepository.updateChargeboxFirmwareStatus(chargeBoxIdentity, status);
        return new FirmwareStatusNotificationResponse();
    }

    public StatusNotificationResponse statusNotification(
            StatusNotificationRequest parameters, String chargeBoxIdentity) {
        // Optional field
        DateTime timestamp = parameters.isSetTimestamp() ? parameters.getTimestamp() : DateTime.now();

        InsertConnectorStatusParams params = InsertConnectorStatusParams.builder()
                .chargeBoxId(chargeBoxIdentity)
                .connectorId(parameters.getConnectorId())
                .status(parameters.getStatus().value())
                .errorCode(parameters.getErrorCode().value())
                .timestamp(timestamp)
                .errorInfo(parameters.getInfo())
                .vendorId(parameters.getVendorId())
                .vendorErrorCode(parameters.getVendorErrorCode())
                .build();

        ocppServerRepository.insertConnectorStatus(params);

        if (parameters.getStatus() == ChargePointStatus.FAULTED) {
            applicationEventPublisher.publishEvent(new OcppStationStatusFailure(
                    chargeBoxIdentity, parameters.getConnectorId(), parameters.getErrorCode().value()));
        }

        final String finalChargeboxId = chargeBoxIdentity;
        final int finalConnector = parameters.getConnectorId();

        int tranasctionId = 0;

        if (parameters.getStatus().value() == "Finishing") {

            log.info(
                    "btbtbtbtbtbtbtbtbt '{}' {} {} ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd.",
                    chargeBoxIdentity, parameters.getConnectorId(), parameters.getStatus().value());
        } else if (parameters.getStatus().value() == "Available") {
            log.info(
                    "wewefwefwefwefwefwefwef '{}' {} {} ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd.",
                    chargeBoxIdentity, parameters.getConnectorId(), parameters.getStatus().value());
            log.info(
                    "wewefwefwefssssssdasdasfasfsafwefwefwefwef '{}' {} {} ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd.",
                    finalChargeboxId, finalConnector, parameters.getStatus().value());

            Connection connection = null;
            try {
                connection = DriverManager.getConnection(BILLING_DB_URL, BILLING_DB_USER, BILLING_DB_PASSWORD);

                // Step 1: Get id from chargebox
                String queryChargebox = "SELECT id FROM chargebox WHERE `chargebox-id` = ?";
                PreparedStatement stmtChargebox = connection.prepareStatement(queryChargebox);
                stmtChargebox.setString(1, chargeBoxIdentity);
                ResultSet rsChargebox = stmtChargebox.executeQuery();
                int chargeboxPk = 0;
                if (rsChargebox.next()) {
                    chargeboxPk = rsChargebox.getInt("id");
                }
                rsChargebox.close();
                stmtChargebox.close();

                log.info("rsChargebox: " + chargeboxPk);

                String queryConnector = "SELECT `connector-id`, station FROM connector WHERE connector = ? AND `ev-id` = ?";
                PreparedStatement stmtConnector = connection.prepareStatement(queryConnector);
                stmtConnector.setInt(1, parameters.getConnectorId());
                stmtConnector.setInt(2, chargeboxPk);
                ResultSet rsConnector = stmtConnector.executeQuery();
                int connectorPk = 0;
                int station = 0;
                if (rsConnector.next()) {
                    connectorPk = rsConnector.getInt("connector-id");
                    station = rsConnector.getInt("station");
                }
                rsConnector.close();
                stmtConnector.close();

                log.info("connectorPk: " + connectorPk);
                log.info("station: " + station);

                final int finalconnectorpk = connectorPk;

                OutboundHttp.submit("billing-clear-fault-available", () -> {
                    Thread.sleep(100);

                    String jsonInputString = String.format("{\"chargebox\":\"%s\", \"connector\":%d}",
                            finalChargeboxId, finalConnector);
                    String endpoint = BILLING_API_BASE_URL +
                            "transaction/clear-fault/availiable";
                    log.info("Endpoint: " + endpoint);
                    OutboundHttp.postJson(endpoint, jsonInputString);
                });

                // Step 3: Get transaction details
                String queryTransaction = "SELECT `transaction-stop`, id, amount, `paid-energy` - `leftover-energy` AS netEnergy FROM transaction WHERE `charge-at` = ? AND `connector-id` = ? AND `car-out` IS NULL AND `status` = 5 ORDER BY `transaction-stop` DESC LIMIT 1";
                PreparedStatement stmtTransaction = connection.prepareStatement(queryTransaction);
                stmtTransaction.setInt(1, chargeboxPk);
                stmtTransaction.setInt(2, connectorPk);
                ResultSet rsTransaction = stmtTransaction.executeQuery();
                Timestamp transactionStop = null;
                if (rsTransaction.next()) {
                    transactionStop = rsTransaction.getTimestamp("transaction-stop");
                    log.info("Transaction-stop: " + transactionStop);
                    log.info("ID: " + rsTransaction.getInt("id"));
                    log.info("Amount: " + rsTransaction.getBigDecimal("amount"));
                    log.info("Net Energy: " + rsTransaction.getBigDecimal("netEnergy"));
                }
                rsTransaction.close();
                stmtTransaction.close();

                // Step 4: Log current time and difference
                Timestamp currentTime = new Timestamp(System.currentTimeMillis());
                if (transactionStop != null) {
                    long diffMilliseconds = currentTime.getTime() - transactionStop.getTime();
                    long diffSeconds = diffMilliseconds / 1000;
                    long diffMinutes = diffSeconds / 60;
                    log.info("Current time: " + currentTime + ", Diff: " + diffMinutes + " minutes");
                }

                // Step 5: Get price-list
                String queryStation = "SELECT `price-list` FROM station WHERE `id` = ?";
                PreparedStatement stmtStation = connection.prepareStatement(queryStation);
                stmtStation.setInt(1, station);
                ResultSet rsStation = stmtStation.executeQuery();
                // if (!rsStation.next()) {
                // log.info("Station not found.");
                // return;
                // }
                int priceListId = 0;
                if (rsStation.next()) {
                    priceListId = rsStation.getInt("price-list");
                }
                rsStation.close();
                stmtStation.close();
                log.info("priceListId: " + priceListId);
                // Step 6: Get penalty-time, penalty-rate
                String queryPriceList = "SELECT `penalty-time`, `penalty-rate` FROM `station-price-list` WHERE `id` = ?";
                PreparedStatement stmtPriceList = connection.prepareStatement(queryPriceList);
                stmtPriceList.setInt(1, priceListId);
                ResultSet rsPriceList = stmtPriceList.executeQuery();
                if (rsPriceList.next()) {
                    log.info("Penalty-time: " + rsPriceList.getInt("penalty-time") + ", Penalty-rate: "
                            + rsPriceList.getBigDecimal("penalty-rate"));
                }
                rsPriceList.close();
                stmtPriceList.close();

            } catch (SQLException e) {
                log.error("SQL Error: " + e.getMessage());
            } finally {
                // Step 7: Close connection
                try {
                    if (connection != null)
                        connection.close();
                } catch (SQLException e) {
                    log.error("SQL Error during closing resources: " + e.getMessage());
                }
            }

            Connection connection1 = null;

            try {
                connection1 = DriverManager.getConnection(BILLING_DB_URL, BILLING_DB_USER, BILLING_DB_PASSWORD);

                String query = "SELECT cb.id AS chargeboxPk, c.`connector-id`, c.station, t.`transaction-stop`, t.id, t.amount, "
                        +
                        "t.`paid-energy` - t.`leftover-energy` AS netEnergy, t.`user-parking-amount` AS parkingAmount ,spl.`penalty-time`, spl.`penalty-rate`, spl.`parking-rate`, "
                        +
                        "st.`price-list` " +
                        "FROM chargebox cb " +
                        "INNER JOIN connector c ON cb.id = c.`ev-id` " +
                        "INNER JOIN transaction t ON c.`connector-id` = t.`connector-id` AND t.`charge-at` = cb.id " +
                        "LEFT JOIN station st ON c.station = st.id " +
                        "LEFT JOIN `station-price-list` spl ON st.`price-list` = spl.id " +
                        "WHERE cb.`chargebox-id` = ? AND c.connector = ? AND t.`car-out` IS NULL AND t.`status` = 5 " +
                        "ORDER BY t.`transaction-stop` DESC " +
                        "LIMIT 1;";

                PreparedStatement stmt = connection1.prepareStatement(query);
                stmt.setString(1, chargeBoxIdentity);
                stmt.setInt(2, parameters.getConnectorId());

                ResultSet rs = stmt.executeQuery();
                Timestamp transactionStop = null;
                if (rs.next()) {
                    transactionStop = rs.getTimestamp("transaction-stop");
                    log.info("Transaction-stop: " + transactionStop);
                    log.info("Chargebox ID: " + rs.getInt("chargeboxPk"));
                    log.info("Connector ID: " + rs.getInt("connector-id"));
                    log.info("Station: " + rs.getInt("station"));
                    log.info("Transaction ID: " + rs.getInt("id"));
                    log.info("Amount: " + rs.getBigDecimal("amount"));
                    log.info("Net Energy: " + rs.getBigDecimal("netEnergy"));
                    log.info("Penalty-time: " + rs.getInt("penalty-time"));
                    log.info("Penalty-rate: " + rs.getBigDecimal("penalty-rate"));
                    log.info("Parking-rate: " + rs.getBigDecimal("parking-rate"));
                    log.info("Parking Amount: " + rs.getBigDecimal("parkingAmount"));
                    log.info("Price List ID: " + rs.getInt("price-list"));

                    tranasctionId = rs.getInt("id");

                    Timestamp currentTime = new Timestamp(System.currentTimeMillis());
                    // Preserve decimal result of division by casting one operand to double
                    double diffMinutesExact = (currentTime.getTime()
                            - (transactionStop != null ? transactionStop.getTime() : 0)) / 60000.0;
                    // Use Math.ceil to round up to the nearest whole number
                    long diffMinutes = (long) Math.ceil(diffMinutesExact);
                    // Preserve decimal result of division for hours by casting one operand to
                    // double
                    double diffHoursExact = (currentTime.getTime()
                            - (transactionStop != null ? transactionStop.getTime() : 0)) / 3600000.0;
                    // Use Math.ceil to round up to the nearest whole number for hours
                    long diffHours = (long) Math.ceil(diffHoursExact);

                    log.info("Current time: " + currentTime + ", Diff: " + diffMinutes + " minutes, " + diffHours
                            + " hours");
                    log.info("Current time: " + currentTime + ", Diff: " + diffMinutesExact + " minutes, "
                            + diffHoursExact
                            + " hours");

                    // Calculate the delta time
                    int penaltyTime = rs.getInt("penalty-time");
                    double deltaTime = diffMinutesExact - penaltyTime;

                    log.info("Delta Time: " + deltaTime);
                    // Calculate the total parking price
                    BigDecimal penaltyRate = rs.getBigDecimal("penalty-rate");
                    BigDecimal totalParkingPrice = penaltyRate.multiply(new BigDecimal(deltaTime));
                    BigDecimal parkingRate = rs.getBigDecimal("parking-rate");
                    BigDecimal transactionParkingAmount = rs.getBigDecimal("parkingAmount");
                    BigDecimal addOnParkingPrice = parkingRate.multiply(new BigDecimal(diffHoursExact));
                    // Round up to 2 decimal places
                    totalParkingPrice = totalParkingPrice.setScale(2, RoundingMode.UP);

                    log.info("Total Parking Price: " + totalParkingPrice);

                    // After calculating the total parking price

                    // 1. Select `amount-energy`, `use-tou-amount`, `use-nor-amount`, `charge-by`
                    // from transaction
                    String transactionQuery = "SELECT `amount`,`user-parking-amount`, `amount-energy`, `use-tou-amount`, `use-nor-amount`, `charge-by` FROM `transaction` WHERE `id` = ?";
                    PreparedStatement transactionStmt = connection1.prepareStatement(transactionQuery);
                    transactionStmt.setInt(1, rs.getInt("id"));
                    ResultSet rsTransaction = transactionStmt.executeQuery();

                    if (rsTransaction.next()) {
                        BigDecimal amountEnergy = rsTransaction.getBigDecimal("amount-energy");
                        BigDecimal useTouAmount = rsTransaction.getBigDecimal("use-tou-amount");
                        BigDecimal useNorAmount = rsTransaction.getBigDecimal("use-nor-amount");
                        BigDecimal amount = rsTransaction.getBigDecimal("amount");
                        BigDecimal useParkingAmount = rsTransaction.getBigDecimal("user-parking-amount");
                        int chargeByUserId = rsTransaction.getInt("charge-by");

                        if (deltaTime <= 0) {
                            totalParkingPrice = BigDecimal.ZERO;
                            transactionParkingAmount = transactionParkingAmount.add(addOnParkingPrice);
                        }

                        // Calculate payback
                        BigDecimal payback = amountEnergy
                                .subtract(useTouAmount.add(useNorAmount).add(totalParkingPrice).add(useParkingAmount));

                        // Adjust payback by subtracting the transaction amount
                        BigDecimal balanceAdjustment = payback.add(amount).negate();

                        // new
                        BigDecimal transactionCost = transactionParkingAmount.add(totalParkingPrice)
                                .add(rsTransaction.getBigDecimal("use-tou-amount"))
                                .add(rsTransaction.getBigDecimal("use-nor-amount"));

                        log.info(
                                "////////////////////////////////////////////////////////////////////////////////////");
                        log.info("amountEnergy: " + amountEnergy);
                        log.info("useTouAmount: " + useTouAmount);
                        log.info("useNorAmount: " + useNorAmount);
                        log.info("totalParkingPrice: " + totalParkingPrice);
                        log.info("payback: " + payback);
                        log.info("amount: " + amount);
                        log.info("balanceAdjustment: " + balanceAdjustment);
                        log.info("useParkingAmount: " + useParkingAmount);
                        log.info("addOnParkingPrice: " + addOnParkingPrice);

                    } else {
                        log.warn("Transaction not found for ID: " + rs.getInt("id"));
                    }

                    rsTransaction.close();
                    transactionStmt.close();

                    // update transaction
                    String updateTransactionQuery = "UPDATE `transaction` SET `user-penalty-amount` = ?, `car-out` = ?, `user-parking-amount` = ?, `status` = 6 WHERE `id` = ?";

                    try (PreparedStatement updateTransactionStmt = connection1
                            .prepareStatement(updateTransactionQuery)) {
                        updateTransactionStmt.setBigDecimal(1, totalParkingPrice);
                        updateTransactionStmt.setTimestamp(2, currentTime);
                        updateTransactionStmt.setBigDecimal(3, transactionParkingAmount);
                        updateTransactionStmt.setInt(4, rs.getInt("id"));
                        // Execute the update
                        int updateCount = updateTransactionStmt.executeUpdate();
                        if (updateCount > 0) {
                            log.info("Transaction updated successfully.");
                        } else {
                            log.warn("No transaction was updated.");
                        }
                        // final String finalChargebox = chargeBoxIdentity;
                        // final int finalConnector = parameters.getConnectorId();
                        final int finalTransactionId = tranasctionId;

                        OutboundHttp.submit("billing-receipt-short", () -> {
                            Thread.sleep(100);

                            String jsonInputString = String.format("{\"transaction\":%d}", finalTransactionId);
                            String endpoint = BILLING_API_BASE_URL +
                                    "transaction/receipt-short";
                            log.info("Endpoint: " + endpoint);
                            OutboundHttp.postJson(endpoint, jsonInputString);
                        });
                    } catch (SQLException e) {
                        log.error("Failed to update transaction: " + e.getMessage());
                    }

                    // Remember to handle SQLExceptions and closing of resources in a finally block
                    // or using try-with-resources
                    rs.close();
                    stmt.close();
                }
            } catch (SQLException e) {
                log.error("SQL Error: " + e.getMessage());
            } finally {
                try {
                    if (connection1 != null)
                        connection1.close();
                } catch (SQLException e) {
                    log.error("SQL Error during closing resources: " + e.getMessage());
                }
            }

        } else if (parameters.getStatus().value() == "Preparing") {
            OutboundHttp.submit("billing-clear-success-preparing", () -> {
                Thread.sleep(100);

                String jsonInputString = String.format("{\"chargebox\":\"%s\", \"connector\":%d}",
                        finalChargeboxId, finalConnector);
                String endpoint = BILLING_API_BASE_URL +
                        "transaction/clear-success/preparing";
                log.info("Endpoint: " + endpoint);
                OutboundHttp.postJson(endpoint, jsonInputString);
            });
        } else if (parameters.getStatus().value() == "SuspendEV") {
            OutboundHttp.submit("billing-suspendev-check", () -> {
                Thread.sleep(100);

                String jsonInputString = String.format("{\"chargebox\":\"%s\", \"connector\":%d}",
                        finalChargeboxId, finalConnector);
                String endpoint = BILLING_API_BASE_URL +
                        "transaction/suspendev/check/donechargingornot";
                log.info("Endpoint: " + endpoint);
                OutboundHttp.postJson(endpoint, jsonInputString);
            });
        }

        return new StatusNotificationResponse();
    }

    public MeterValuesResponse meterValues(MeterValuesRequest parameters, String chargeBoxIdentity) {
        ocppServerRepository.insertMeterValues(
                chargeBoxIdentity,
                parameters.getMeterValue(),
                parameters.getConnectorId(),
                parameters.getTransactionId());

        return new MeterValuesResponse();
    }

    public DiagnosticsStatusNotificationResponse diagnosticsStatusNotification(
            DiagnosticsStatusNotificationRequest parameters, String chargeBoxIdentity) {
        String status = parameters.getStatus().value();
        ocppServerRepository.updateChargeboxDiagnosticsStatus(chargeBoxIdentity, status);
        return new DiagnosticsStatusNotificationResponse();
    }

    public StartTransactionResponse startTransaction(StartTransactionRequest parameters, String chargeBoxIdentity) {
        // Get the authorization info of the user, before making tx changes (will
        // affectAuthorizationStatus)
        IdTagInfo info = ocppTagService.getIdTagInfo(
                parameters.getIdTag(),
                1,
                () -> new IdTagInfo().withStatus(AuthorizationStatus.INVALID), // IdTagInfo is required
                chargeBoxIdentity);

        InsertTransactionParams params = InsertTransactionParams.builder()
                .chargeBoxId(chargeBoxIdentity)
                .connectorId(parameters.getConnectorId())
                .idTag(parameters.getIdTag())
                .startTimestamp(parameters.getTimestamp())
                .startMeterValue(Integer.toString(parameters.getMeterStart()))
                .reservationId(parameters.getReservationId())
                .eventTimestamp(DateTime.now())
                .build();

        int transactionId = ocppServerRepository.insertTransaction(params);

        applicationEventPublisher.publishEvent(new OcppTransactionStarted(transactionId, params));

        return new StartTransactionResponse()
                .withIdTagInfo(info)
                .withTransactionId(transactionId);
    }

    public StopTransactionResponse stopTransaction(StopTransactionRequest parameters, String chargeBoxIdentity) {
        int transactionId = parameters.getTransactionId();
        String stopReason = parameters.isSetReason() ? parameters.getReason().value() : null;

        // Get the authorization info of the user, before making tx changes (will
        // affectAuthorizationStatus)
        IdTagInfo idTagInfo = ocppTagService.getIdTagInfo(
                parameters.getIdTag(),
                2,
                () -> null,
                chargeBoxIdentity);

        UpdateTransactionParams params = UpdateTransactionParams.builder()
                .chargeBoxId(chargeBoxIdentity)
                .transactionId(transactionId)
                .stopTimestamp(parameters.getTimestamp())
                .stopMeterValue(Integer.toString(parameters.getMeterStop()))
                .stopReason(stopReason)
                .eventTimestamp(DateTime.now())
                .eventActor(TransactionStopEventActor.station)
                .build();

        ocppServerRepository.updateTransaction(params);

        ocppServerRepository.insertMeterValues(chargeBoxIdentity, parameters.getTransactionData(), transactionId);

        applicationEventPublisher.publishEvent(new OcppTransactionEnded(params));

        return new StopTransactionResponse().withIdTagInfo(idTagInfo);
    }

    public HeartbeatResponse heartbeat(HeartbeatRequest parameters, String chargeBoxIdentity) {
        DateTime now = DateTime.now();
        ocppServerRepository.updateChargeboxHeartbeat(chargeBoxIdentity, now);

        return new HeartbeatResponse().withCurrentTime(now);
    }

    public AuthorizeResponse authorize(AuthorizeRequest parameters, String chargeBoxIdentity) {
        // Get the authorization info of the user
        IdTagInfo idTagInfo = ocppTagService.getIdTagInfo(
                parameters.getIdTag(),
                0,
                () -> new IdTagInfo().withStatus(AuthorizationStatus.INVALID),
                chargeBoxIdentity);

        return new AuthorizeResponse().withIdTagInfo(idTagInfo);
    }

    /**
     * Dummy implementation. This is new in OCPP 1.5. It must be vendor-specific.
     */
    public DataTransferResponse dataTransfer(DataTransferRequest parameters, String chargeBoxIdentity) {
        log.info("[Data Transfer] Charge point: {}, Vendor Id: {}", chargeBoxIdentity, parameters.getVendorId());
        if (parameters.isSetMessageId()) {
            log.info("[Data Transfer] Message Id: {}", parameters.getMessageId());
        }
        if (parameters.isSetData()) {
            log.info("[Data Transfer] Data: {}", parameters.getData());
        }

        // OCPP requires a status to be set. Since this is a dummy impl, set it to
        // "Accepted".
        return new DataTransferResponse().withStatus(DataTransferStatus.ACCEPTED);
    }
}
