package de.rwth.idsg.steve.repository.impl;

import com.google.common.base.Throwables;
import com.google.common.util.concurrent.Striped;

import de.rwth.idsg.steve.SteveConfiguration.DB;
import de.rwth.idsg.steve.SteveException;
import de.rwth.idsg.steve.ocpp.OcppProtocol;
import de.rwth.idsg.steve.repository.OcppServerRepository;
import de.rwth.idsg.steve.repository.ReservationRepository;
import de.rwth.idsg.steve.repository.dto.InsertConnectorStatusParams;
import de.rwth.idsg.steve.repository.dto.InsertTransactionParams;
import de.rwth.idsg.steve.repository.dto.TransactionStatusUpdate;
import de.rwth.idsg.steve.repository.dto.UpdateChargeboxParams;
import de.rwth.idsg.steve.repository.dto.UpdateTransactionParams;
import de.rwth.idsg.steve.utils.OutboundHttp;
import jooq.steve.db.enums.TransactionStopEventActor;
import jooq.steve.db.enums.TransactionStopFailedEventActor;
import jooq.steve.db.tables.records.ConnectorMeterValueRecord;
import lombok.AccessLevel;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import ocpp.cs._2015._10.MeterValue;
import org.joda.time.DateTime;
import org.jooq.DSLContext;
import org.jooq.Record1;
import org.jooq.SelectConditionStep;
import org.jooq.impl.DSL;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Repository;
import org.springframework.util.CollectionUtils;

import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.Lock;
import java.util.stream.Collectors;

import static jooq.steve.db.tables.ChargeBox.CHARGE_BOX;
import static jooq.steve.db.tables.Connector.CONNECTOR;
import static jooq.steve.db.tables.ConnectorMeterValue.CONNECTOR_METER_VALUE;
import static jooq.steve.db.tables.ConnectorStatus.CONNECTOR_STATUS;
import static jooq.steve.db.tables.OcppTag.OCPP_TAG;
import static jooq.steve.db.tables.TransactionStart.TRANSACTION_START;
import static jooq.steve.db.tables.TransactionStop.TRANSACTION_STOP;
import static jooq.steve.db.tables.TransactionStopFailed.TRANSACTION_STOP_FAILED;

// new line
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;

import static de.rwth.idsg.steve.SteveConfiguration.CONFIG;

/**
 * This class has methods for database access that are used by the OCPP service.
 *
 * http://www.jooq.org/doc/3.4/manual/sql-execution/transaction-management/
 */
@Slf4j
@Repository
public class OcppServerRepositoryImpl implements OcppServerRepository {

    private static final String BILLING_API_BASE_URL = CONFIG.getBillingApi().getBaseUrl();
    private static final String BILLING_DB_URL = CONFIG.getBillingDb().getUrl();
    private static final String BILLING_DB_USER = CONFIG.getBillingDb().getUserName();
    private static final String BILLING_DB_PASSWORD = CONFIG.getBillingDb().getPassword();
    private static final long STOP_SESSION_THROTTLE_MS = 30_000L;
    private static final ConcurrentMap<String, Long> STOP_SESSION_REQUESTS = new ConcurrentHashMap<>();

    @Autowired
    private DSLContext ctx;
    @Autowired
    private ReservationRepository reservationRepository;

    private final Striped<Lock> transactionTableLocks = Striped.lock(16);

    @Override
    public void updateChargebox(UpdateChargeboxParams p) {
        ctx.update(CHARGE_BOX)
                .set(CHARGE_BOX.OCPP_PROTOCOL, p.getOcppProtocol().getCompositeValue())
                .set(CHARGE_BOX.CHARGE_POINT_VENDOR, p.getVendor())
                .set(CHARGE_BOX.CHARGE_POINT_MODEL, p.getModel())
                .set(CHARGE_BOX.CHARGE_POINT_SERIAL_NUMBER, p.getPointSerial())
                .set(CHARGE_BOX.CHARGE_BOX_SERIAL_NUMBER, p.getBoxSerial())
                .set(CHARGE_BOX.FW_VERSION, p.getFwVersion())
                .set(CHARGE_BOX.ICCID, p.getIccid())
                .set(CHARGE_BOX.IMSI, p.getImsi())
                .set(CHARGE_BOX.METER_TYPE, p.getMeterType())
                .set(CHARGE_BOX.METER_SERIAL_NUMBER, p.getMeterSerial())
                .set(CHARGE_BOX.LAST_HEARTBEAT_TIMESTAMP, p.getHeartbeatTimestamp())
                .where(CHARGE_BOX.CHARGE_BOX_ID.equal(p.getChargeBoxId()))
                .execute();
    }

    @Override
    public void updateOcppProtocol(String chargeBoxIdentity, OcppProtocol protocol) {
        ctx.update(CHARGE_BOX)
                .set(CHARGE_BOX.OCPP_PROTOCOL, protocol.getCompositeValue())
                .where(CHARGE_BOX.CHARGE_BOX_ID.equal(chargeBoxIdentity))
                .execute();
    }

    @Override
    public void updateEndpointAddress(String chargeBoxIdentity, String endpointAddress) {
        ctx.update(CHARGE_BOX)
                .set(CHARGE_BOX.ENDPOINT_ADDRESS, endpointAddress)
                .where(CHARGE_BOX.CHARGE_BOX_ID.equal(chargeBoxIdentity))
                .execute();
    }

    @Override
    public void updateChargeboxFirmwareStatus(String chargeBoxIdentity, String firmwareStatus) {
        ctx.update(CHARGE_BOX)
                .set(CHARGE_BOX.FW_UPDATE_STATUS, firmwareStatus)
                .set(CHARGE_BOX.FW_UPDATE_TIMESTAMP, DateTime.now())
                .where(CHARGE_BOX.CHARGE_BOX_ID.equal(chargeBoxIdentity))
                .execute();
    }

    @Override
    public void updateChargeboxDiagnosticsStatus(String chargeBoxIdentity, String status) {
        ctx.update(CHARGE_BOX)
                .set(CHARGE_BOX.DIAGNOSTICS_STATUS, status)
                .set(CHARGE_BOX.DIAGNOSTICS_TIMESTAMP, DateTime.now())
                .where(CHARGE_BOX.CHARGE_BOX_ID.equal(chargeBoxIdentity))
                .execute();
    }

    @Override
    public void updateChargeboxHeartbeat(String chargeBoxIdentity, DateTime ts) {
        ctx.update(CHARGE_BOX)
                .set(CHARGE_BOX.LAST_HEARTBEAT_TIMESTAMP, ts)
                .where(CHARGE_BOX.CHARGE_BOX_ID.equal(chargeBoxIdentity))
                .execute();
    }

    @Override
    public void insertConnectorStatus(InsertConnectorStatusParams p) {
        ctx.transaction(configuration -> {
            DSLContext ctx = DSL.using(configuration);

            // Step 1
            insertIgnoreConnector(ctx, p.getChargeBoxId(), p.getConnectorId());

            // -------------------------------------------------------------------------
            // Step 2: We store a log of connector statuses
            // -------------------------------------------------------------------------

            ctx.insertInto(CONNECTOR_STATUS)
                    .set(CONNECTOR_STATUS.CONNECTOR_PK, DSL.select(CONNECTOR.CONNECTOR_PK)
                            .from(CONNECTOR)
                            .where(CONNECTOR.CHARGE_BOX_ID.equal(p.getChargeBoxId()))
                            .and(CONNECTOR.CONNECTOR_ID.equal(p.getConnectorId())))
                    .set(CONNECTOR_STATUS.STATUS_TIMESTAMP, p.getTimestamp())
                    .set(CONNECTOR_STATUS.STATUS, p.getStatus())
                    .set(CONNECTOR_STATUS.ERROR_CODE, p.getErrorCode())
                    .set(CONNECTOR_STATUS.ERROR_INFO, p.getErrorInfo())
                    .set(CONNECTOR_STATUS.VENDOR_ID, p.getVendorId())
                    .set(CONNECTOR_STATUS.VENDOR_ERROR_CODE, p.getVendorErrorCode())
                    .execute();

            log.debug("Stored a new connector status for {}/{}.", p.getChargeBoxId(), p.getConnectorId());
        });
    }

    @Override
    public void insertMeterValues(String chargeBoxIdentity, List<MeterValue> list, int connectorId,
            Integer transactionId) {
        if (CollectionUtils.isEmpty(list)) {
            return;
        }
        log.info("insertMeterValues : insertIgnoreConnector -> " + chargeBoxIdentity);

        // Use an array to hold the currentEnergy value so it can be modified inside the
        // lambda
        final float[] currentEnergy = { 0 };

        // Initialize the counter as an AtomicInteger
        AtomicInteger count1111 = new AtomicInteger(0);

        // Log the list values for a specific measurand
        list.forEach(meterValue -> {
            meterValue.getSampledValue().forEach(sampledValue -> {
                // Check if the measurand is "Energy.Active.Import.Register"
                if (sampledValue.isSetMeasurand()
                        && "Energy.Active.Import.Register".equals(sampledValue.getMeasurand().value())) {
                    try {
                        // Update the value inside the array
                        currentEnergy[0] = Float.parseFloat(sampledValue.getValue());
                        log.info("MeterValue Timestamp: " + meterValue.getTimestamp()
                                + ", Value for Energy.Active.Import.Register: " + currentEnergy[0]);
                        // Increment the counter
                        count1111.getAndIncrement();
                    } catch (NumberFormatException e) {
                        log.error("Error parsing sampledValue to float: " + sampledValue.getValue(), e);
                    }
                }
            });
        });

        final float[] startEnergy = { 0 };

        ctx.transaction(configuration -> {
            try {
                DSLContext ctx = DSL.using(configuration);

                insertIgnoreConnector(ctx, chargeBoxIdentity, connectorId);
                int connectorPk = getConnectorPkFromConnector(ctx, chargeBoxIdentity, connectorId);
                batchInsertMeterValues(ctx, list, connectorPk, transactionId);
            } catch (Exception e) {
                log.error("Exception occurred", e);
            }
        });

        log.info("======================================================transactionId: " + transactionId);

        // if (transactionId != null) {
        if (transactionId != null) {
            log.info(
                    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");

            ctx.transaction(configuration -> {
                try {
                    DSLContext ctx = DSL.using(configuration);

                    // First, get connector primary key from transaction table
                    String startValue = ctx.select(TRANSACTION_START.START_VALUE)
                            .from(TRANSACTION_START)
                            .where(TRANSACTION_START.TRANSACTION_PK.equal(transactionId))
                            .fetchOne()
                            .value1();

                    startEnergy[0] = Float.parseFloat(startValue);

                } catch (Exception e) {
                    log.error("Exception occurred", e);
                }
            });

            // Log the final count and currentEnergy value
            log.info("Final count -> " + count1111);
            log.info("Final currentEnergy value: " + currentEnergy[0]);
            log.info("Final startEnergy value: " + startEnergy[0]);

            Connection connectionBilling = null;
            PreparedStatement selectStmt = null;
            ResultSet rs = null;

            String companyName = null;
            String chargebox = null;
            String uniqueTag = null;
            int energy_unit = 0;

            try {
                connectionBilling = DriverManager.getConnection(BILLING_DB_URL, BILLING_DB_USER, BILLING_DB_PASSWORD);

                // 1. Select paid-energy where id-on-central-server = transactionId
                final String selectQuery = "SELECT t.`paid-energy` AS paid_energy, "
                        + "c.`header-name` AS company_name, "
                        + "e.`chargebox-id` AS chargebox, "
                        + "con.`unit`, "
                        + "u.`unique-token` AS user_unique_token " // Added unique-token from users table
                        + "FROM `transaction` t "
                        + "INNER JOIN `users` u ON t.`charge-by` = u.`id` "
                        + "INNER JOIN `company` c ON u.`customer-of` = c.`id` "
                        + "INNER JOIN `chargebox` e ON t.`charge-at` = e.`id` "
                        + "INNER JOIN `connector` con ON t.`connector-id` = con.`connector-id` "
                        + "WHERE t.`id-on-central-server` = ?";
                selectStmt = connectionBilling.prepareStatement(selectQuery);
                selectStmt.setInt(1, transactionId);
                rs = selectStmt.executeQuery();

                float paidEnergy = 0;
                if (rs.next()) {
                    paidEnergy = rs.getFloat("paid_energy");
                    companyName = rs.getString("company_name"); // Get company name
                    chargebox = rs.getString("chargebox");
                    uniqueTag = rs.getString("user_unique_token");
                    energy_unit = rs.getInt("unit");
                }

                // 2. Reading value from list where measurand = Energy.Active.Import.Register
                // BigDecimal energyActiveImportRegister = list.stream()
                // .filter(mv -> "Energy.Active.Import.Register".equals(mv.getMeasurand()))
                // .map(MeterValue::getValue)
                // .map(BigDecimal::new)
                // .findFirst()
                // .orElse(BigDecimal.ZERO);

                log.info("========================================companyName : " + companyName);
                log.info("========================================chargebox : " + chargebox);

                float goalEnergy = (energy_unit == 1) ? (paidEnergy * 1000) + startEnergy[0]
                        : (paidEnergy) + startEnergy[0]; // change unit

                log.info(
                        "---------------------------------------------------- ENERGY ----------------------------------------------------");
                log.info("-------------------------------------------- Paid Energy: " + paidEnergy);

                log.info("-------------------------------------------- Goal Energy: " + goalEnergy);
                log.info(
                        "----------------------------------------------------------------------------------------------------------------");

                if (currentEnergy[0] >= goalEnergy) {

                    // if (true) {
                    // if (true) {
                    // 4. Log both values
                    log.info("Goal Energy: " + goalEnergy);
                    log.info("Energy Active Import Register: " + currentEnergy[0]);

                    final String finalChargeBoxIdentity = chargeBoxIdentity;
                    final String finalUniqueTag = uniqueTag;
                    final String stopSessionKey = finalChargeBoxIdentity + ":" + connectorId + ":" + transactionId
                            + ":" + finalUniqueTag;

                    if (shouldSubmitStopSession(stopSessionKey)) {
                        OutboundHttp.submit("stop-session", () -> {
                            Thread.sleep(3000);

                            String endpoint = String.format("http://localhost:5002/develop/dev/%s/stopSession/%s",
                                    finalChargeBoxIdentity, finalUniqueTag);

                            log.info("Endpoint: " + endpoint);
                            OutboundHttp.get(endpoint);
                        });
                    } else {
                        log.info("Stop session request suppressed for key {} within {} ms.", stopSessionKey,
                                STOP_SESSION_THROTTLE_MS);
                    }
                }

            } catch (SQLException e) {
                log.error("SQL Error: " + e.getMessage(), e);
            } finally {
                // Close resources
                try {
                    if (rs != null)
                        rs.close();
                    if (selectStmt != null)
                        selectStmt.close();
                    if (connectionBilling != null)
                        connectionBilling.close();
                } catch (SQLException e) {
                    log.error("SQL Error during closing resources: " + e.getMessage(), e);
                }
            }
        }

    }

    @Override
    public void insertMeterValues(String chargeBoxIdentity, List<MeterValue> list, int transactionId) {
        if (CollectionUtils.isEmpty(list)) {
            return;
        }
        log.info("insertMeterValues : find transaction start -> " + chargeBoxIdentity);

        ctx.transaction(configuration -> {
            try {
                DSLContext ctx = DSL.using(configuration);

                // First, get connector primary key from transaction table
                int connectorPk = ctx.select(TRANSACTION_START.CONNECTOR_PK)
                        .from(TRANSACTION_START)
                        .where(TRANSACTION_START.TRANSACTION_PK.equal(transactionId))
                        .fetchOne()
                        .value1();

                batchInsertMeterValues(ctx, list, connectorPk, transactionId);
            } catch (Exception e) {
                log.error("Exception occurred", e);
            }
        });

    }

    private static boolean shouldSubmitStopSession(String key) {
        long now = System.currentTimeMillis();
        Long previous = STOP_SESSION_REQUESTS.putIfAbsent(key, now);

        if (previous == null) {
            return true;
        }

        if (now - previous <= STOP_SESSION_THROTTLE_MS) {
            return false;
        }

        return STOP_SESSION_REQUESTS.replace(key, previous, now);
    }

    @Override
    public int insertTransaction(InsertTransactionParams p) {

        SelectConditionStep<Record1<Integer>> connectorPkQuery = DSL.select(CONNECTOR.CONNECTOR_PK)
                .from(CONNECTOR)
                .where(CONNECTOR.CHARGE_BOX_ID.equal(p.getChargeBoxId()))
                .and(CONNECTOR.CONNECTOR_ID.equal(p.getConnectorId()));

        // -------------------------------------------------------------------------
        // Step 1: Insert connector and idTag, if they are new to us
        // -------------------------------------------------------------------------

        insertIgnoreConnector(ctx, p.getChargeBoxId(), p.getConnectorId());

        // it is important to insert idTag before transaction, since the transaction
        // table references it
        boolean unknownTagInserted = insertIgnoreIdTag(ctx, p);

        // -------------------------------------------------------------------------
        // Step 2: Insert transaction if it does not exist already
        // -------------------------------------------------------------------------

        TransactionDataHolder data = insertIgnoreTransaction(p, connectorPkQuery);
        int transactionId = data.transactionId;

        if (data.existsAlready) {
            return transactionId;
        }

        if (unknownTagInserted) {
            log.warn("The transaction '{}' contains an unknown idTag '{}' which was inserted into DB "
                    + "to prevent information loss and has been blocked", transactionId, p.getIdTag());
        }

        // -------------------------------------------------------------------------
        // Step 3 for OCPP >= 1.5: A startTransaction may be related to a reservation
        // -------------------------------------------------------------------------

        if (p.isSetReservationId()) {
            reservationRepository.used(connectorPkQuery, p.getIdTag(), p.getReservationId(), transactionId);
        }

        // -------------------------------------------------------------------------
        // Step 4: Set connector status
        // -------------------------------------------------------------------------

        if (shouldInsertConnectorStatusAfterTransactionMsg(p.getChargeBoxId())) {
            insertConnectorStatus(ctx, connectorPkQuery, p.getStartTimestamp(), p.getStatusUpdate());
        }

        // log.info("START : " + connectorPkQuery);

        Integer connectorPk = ctx.select(CONNECTOR.CONNECTOR_PK)
                .from(CONNECTOR)
                .where(CONNECTOR.CHARGE_BOX_ID.equal(p.getChargeBoxId()))
                .and(CONNECTOR.CONNECTOR_ID.equal(p.getConnectorId()))
                .fetchOneInto(Integer.class);

        OutboundHttp.submit("billing-transaction-initial-transactionid", () -> {
            String jsonInputString = String.format(
                    "{\"transactionId\":%d, \"connectorPk\":%d}",
                    transactionId, connectorPk);
            String endpoint = BILLING_API_BASE_URL + "transaction/initial/transactionid";
            log.info("Endpoint: " + endpoint);
            log.info("jsonInputString: " + jsonInputString);
            OutboundHttp.postJson(endpoint, jsonInputString);
        });

        return transactionId;
    }

    @Override
    public void updateTransaction(UpdateTransactionParams p) {

        // -------------------------------------------------------------------------
        // Step 1: insert transaction stop data
        // -------------------------------------------------------------------------

        String meterValueStartStr = ctx.select(TRANSACTION_START.START_VALUE)
                .from(TRANSACTION_START)
                .where(TRANSACTION_START.TRANSACTION_PK.equal(p.getTransactionId()))
                .fetchOneInto(String.class);

        // Convert meterValueStartStr to float
        float meterStart = 0.0f;
        try {
            if (meterValueStartStr != null && !meterValueStartStr.isEmpty()) {
                meterStart = Float.parseFloat(meterValueStartStr);
            }
            log.info(
                    "===============================44444444444444444444444444444===========================================");
            log.info("meterValueStartStr : ");
            log.info(meterValueStartStr);
            log.info("meterStart : " + meterStart);
        } catch (NumberFormatException e) {
            log.error("Error parsing meterStart value: " + meterValueStartStr, e);
        }

        // Convert p.getStopMeterValue() to float
        float stopMeterValue = 0.0f;
        try {
            String stopMeterValueStr = p.getStopMeterValue();
            if (stopMeterValueStr != null && !stopMeterValueStr.isEmpty()) {
                stopMeterValue = Float.parseFloat(stopMeterValueStr);
            }
            log.info(
                    "==================================444444444444444444444444444444========================================");
            log.info("stopMeterValueStr : ");
            log.info(stopMeterValueStr);
            log.info("stopMeterValue : " + stopMeterValue);
        } catch (NumberFormatException e) {
            log.error("Error parsing stopMeterValue: " + p.getStopMeterValue(), e);
        }

        final String selectQueryGetUnit = "SELECT t.`connector-id` AS connector, c.`unit` " +
                "FROM `transaction` t " +
                "JOIN `connector` c ON t.`connector-id` = c.`connector-id` " +
                "WHERE t.`id-on-central-server` = ? LIMIT 1;";


        Connection connectionBillingGetUnit = null;
        PreparedStatement selectStmtGetUnit = null;
        PreparedStatement updateStmtGetUnit = null;

        String energy_unit = null;
        int energy_unit_int = 0;

        try {
            // Open connection to the billing database
            connectionBillingGetUnit = DriverManager.getConnection(BILLING_DB_URL, BILLING_DB_USER,
                    BILLING_DB_PASSWORD);

            // Prepare and execute the select statement
            selectStmtGetUnit = connectionBillingGetUnit.prepareStatement(selectQueryGetUnit);
            selectStmtGetUnit.setInt(1, p.getTransactionId()); // Assuming transactionCS is defined and available
            ResultSet rs = selectStmtGetUnit.executeQuery();

            if (rs.next()) {
                int connector = rs.getInt("connector");
                energy_unit_int = rs.getInt("unit");
                energy_unit = (energy_unit_int == 1) ? "Wh" : "kWh";

                // Further processing with unitAsString as needed...
            } else {
                log.error("No matching records found for id-on-central-server = " + p.getTransactionId());
            }
        } catch (SQLException e) {
            // Handle SQL Exception
            log.error("SQL Error: " + e.getMessage(), e);
        } finally {
            // Close PreparedStatement and Connection
            try {
                if (selectStmtGetUnit != null)
                    selectStmtGetUnit.close();
                if (connectionBillingGetUnit != null)
                    connectionBillingGetUnit.close();
            } catch (SQLException e) {
                log.error("SQL Error during closing resources: " + e.getMessage(), e);
            }
        }

        float deltaEnergy = (energy_unit_int == 1) ? ((stopMeterValue / 1000) - (meterStart / 1000))
                : ((stopMeterValue) - (meterStart)); // change unit
        log.info(".......................................................deltaEnergy -> " + deltaEnergy);

        if (deltaEnergy < 121) {
            // JOOQ will throw an exception, if something goes wrong
            try {
                ctx.insertInto(TRANSACTION_STOP)
                        .set(TRANSACTION_STOP.TRANSACTION_PK, p.getTransactionId())
                        .set(TRANSACTION_STOP.EVENT_TIMESTAMP, p.getEventTimestamp())
                        .set(TRANSACTION_STOP.EVENT_ACTOR, p.getEventActor())
                        .set(TRANSACTION_STOP.STOP_TIMESTAMP, p.getStopTimestamp())
                        .set(TRANSACTION_STOP.STOP_VALUE, p.getStopMeterValue())
                        .set(TRANSACTION_STOP.STOP_REASON, p.getStopReason())
                        .execute();
            } catch (Exception e) {
                log.error("Exception occurred", e);
                tryInsertingFailed(p, e);
            }

            final String selectQuery = "SELECT t.`paid-energy` AS paid_energy, c.`header-name` AS company_name, e.`chargebox-id` AS chargebox "
                    +
                    "FROM `transaction` t " +
                    "INNER JOIN `users` u ON t.`charge-by` = u.`id` " +
                    "INNER JOIN `company` c ON u.`customer-of` = c.`id` " +
                    "INNER JOIN `chargebox` e ON t.`charge-at` = e.`id` " +
                    "WHERE t.`id-on-central-server` = ? AND t.`status` = 3 " +
                    "ORDER BY t.`id` DESC LIMIT 1";
            final String updateQuery = "UPDATE transaction SET `leftover-energy` = ?, `transaction-stop` = NOW(), status = 4 WHERE `id-on-central-server` = ? AND status = 3 ORDER BY id DESC LIMIT 1";

            Connection connectionBilling = null;
            PreparedStatement selectStmt = null;
            PreparedStatement updateStmt = null;
            ResultSet rs = null;

            try {
                connectionBilling = DriverManager.getConnection(BILLING_DB_URL, BILLING_DB_USER, BILLING_DB_PASSWORD);
                selectStmt = connectionBilling.prepareStatement(selectQuery);
                selectStmt.setInt(1, p.getTransactionId()); // Assuming transactionId is final or effectively final
                rs = selectStmt.executeQuery();

                if (rs.next()) {
                    final float paidEnergy = rs.getFloat("paid_energy");
                    final String companyName = rs.getString("company_name");
                    final String chargebox = rs.getString("chargebox");
                    final float leftoverEnergy = paidEnergy - deltaEnergy; // Calculate leftoverEnergy here

                    // Prepare for asynchronous operation
                    final String finalCompanyName = companyName; // Ensure variables are effectively final for the
                                                                 // lambda
                    final String finalChargebox = chargebox;
                    final int transactionId_t = p.getTransactionId();

                    log.info(
                            "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
                    log.info("leftoverEnergy -> " + leftoverEnergy);
                    log.info("transactionId_t -> " + transactionId_t);
                    // Update statement within lambda
                    try {
                        updateStmt = connectionBilling.prepareStatement(updateQuery);
                        updateStmt.setFloat(1, leftoverEnergy);
                        updateStmt.setInt(2, transactionId_t);
                        updateStmt.executeUpdate();
                    } catch (SQLException e) {
                        log.error("SQL Error during update: " + e.getMessage(), e);
                    }

                    OutboundHttp.submit("billing-transaction-done", () -> {
                        String jsonInputString = String.format("{\"leftoverEnergy\":%f, \"id\":%d}", leftoverEnergy,
                                transactionId_t);
                        String endpoint = BILLING_API_BASE_URL + finalCompanyName + "/"
                                + finalChargebox
                                + "/transaction/done";
                        log.info("Endpoint: " + endpoint);
                        OutboundHttp.postJson(endpoint, jsonInputString);
                    });
                }
            } catch (SQLException e) {
                // Handle SQL Exception
                log.error("SQL Error: " + e.getMessage(), e);
            } finally {
                // Close ResultSet, PreparedStatement, and Connection
                try {
                    if (rs != null)
                        rs.close();
                    if (selectStmt != null)
                        selectStmt.close();
                    if (updateStmt != null)
                        updateStmt.close();
                    if (connectionBilling != null)
                        connectionBilling.close();
                } catch (SQLException e) {
                    log.error("SQL Error during closing resources: " + e.getMessage(), e);
                }
            }
        }

        // -------------------------------------------------------------------------
        // Step 2: Set connector status back. We do this even in cases where step 1
        // fails. It probably and hopefully makes sense.
        // -------------------------------------------------------------------------

        if (shouldInsertConnectorStatusAfterTransactionMsg(p.getChargeBoxId())) {
            SelectConditionStep<Record1<Integer>> connectorPkQuery = DSL.select(TRANSACTION_START.CONNECTOR_PK)
                    .from(TRANSACTION_START)
                    .where(TRANSACTION_START.TRANSACTION_PK.equal(p.getTransactionId()));

            insertConnectorStatus(ctx, connectorPkQuery, p.getStopTimestamp(), p.getStatusUpdate());
        }
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    @RequiredArgsConstructor(access = AccessLevel.PRIVATE)
    private static final class TransactionDataHolder {
        final boolean existsAlready;
        final int transactionId;
    }

    /**
     * Use case: If the station sends identical StartTransaction messages multiple
     * times (e.g. due to connection
     * problems the response of StartTransaction could not be delivered and station
     * tries again later), we do not want
     * to insert this into database multiple times.
     */
    private TransactionDataHolder insertIgnoreTransaction(InsertTransactionParams p,
            SelectConditionStep<Record1<Integer>> connectorPkQuery) {
        Lock l = transactionTableLocks.get(p.getChargeBoxId());
        l.lock();
        try {
            Record1<Integer> r = ctx.select(TRANSACTION_START.TRANSACTION_PK)
                    .from(TRANSACTION_START)
                    .where(TRANSACTION_START.CONNECTOR_PK.eq(connectorPkQuery))
                    .and(TRANSACTION_START.ID_TAG.eq(p.getIdTag()))
                    .and(TRANSACTION_START.START_TIMESTAMP.eq(p.getStartTimestamp()))
                    .and(TRANSACTION_START.START_VALUE.eq(p.getStartMeterValue()))
                    .fetchOne();

            if (r != null) {
                return new TransactionDataHolder(true, r.value1());
            }

            Integer transactionId = ctx.insertInto(TRANSACTION_START)
                    .set(TRANSACTION_START.EVENT_TIMESTAMP, p.getEventTimestamp())
                    .set(TRANSACTION_START.CONNECTOR_PK, connectorPkQuery)
                    .set(TRANSACTION_START.ID_TAG, p.getIdTag())
                    .set(TRANSACTION_START.START_TIMESTAMP, p.getStartTimestamp())
                    .set(TRANSACTION_START.START_VALUE, p.getStartMeterValue())
                    .returning(TRANSACTION_START.TRANSACTION_PK)
                    .fetchOne()
                    .getTransactionPk();

            // Actually unnecessary, because JOOQ will throw an exception, if something goes
            // wrong
            if (transactionId == null) {
                throw new SteveException("Failed to INSERT transaction into database");
            }

            return new TransactionDataHolder(false, transactionId);
        } finally {
            l.unlock();
        }
    }

    /**
     * After a transaction start/stop event, a charging station _might_ send a
     * connector status notification, but it is
     * not required. With this, we make sure that the status is updated accordingly.
     * Since we use the timestamp of the
     * transaction data, we do not necessarily insert a "most recent" status.
     *
     * If the station sends a notification, we will have a more recent timestamp,
     * and therefore the status of the
     * notification will be used as current. Or, if this transaction data was sent
     * to us for a failed push from the past
     * and we have a "more recent" status, it will still be the current status.
     */
    private void insertConnectorStatus(DSLContext ctx,
            SelectConditionStep<Record1<Integer>> connectorPkQuery,
            DateTime timestamp,
            TransactionStatusUpdate statusUpdate) {
        try {
            ctx.insertInto(CONNECTOR_STATUS)
                    .set(CONNECTOR_STATUS.CONNECTOR_PK, connectorPkQuery)
                    .set(CONNECTOR_STATUS.STATUS_TIMESTAMP, timestamp)
                    .set(CONNECTOR_STATUS.STATUS, statusUpdate.getStatus())
                    .set(CONNECTOR_STATUS.ERROR_CODE, statusUpdate.getErrorCode())
                    .execute();
        } catch (Exception e) {
            log.error("Exception occurred", e);
        }
    }

    /**
     * If the connector information was not received before, insert it. Otherwise,
     * ignore.
     */

    // new version
    private void insertIgnoreConnector(DSLContext ctxOcpptest, String chargeBoxIdentity, int connectorId) {
        ctxOcpptest.transaction(configuration -> {
            DSLContext ctx = DSL.using(configuration);

            // Check if the connector already exists to avoid unnecessary inserts
            Integer existingConnectorPk = ctx.select(CONNECTOR.CONNECTOR_PK)
                    .from(CONNECTOR)
                    .where(CONNECTOR.CHARGE_BOX_ID.eq(chargeBoxIdentity))
                    .and(CONNECTOR.CONNECTOR_ID.eq(connectorId))
                    .fetchOneInto(Integer.class);

            if (existingConnectorPk == null) {
                // Insert the connector if it does not exist
                int count = ctx.insertInto(CONNECTOR, CONNECTOR.CHARGE_BOX_ID, CONNECTOR.CONNECTOR_ID)
                        .values(chargeBoxIdentity, connectorId)
                        .execute();

                if (count == 1) {
                    log.info("The connector {}/{} is NEW, and inserted into DB.", chargeBoxIdentity, connectorId);

                    // Retrieve the connector_pk after insertion
                    Integer connectorPk = ctx.select(CONNECTOR.CONNECTOR_PK)
                            .from(CONNECTOR)
                            .where(CONNECTOR.CHARGE_BOX_ID.eq(chargeBoxIdentity))
                            .and(CONNECTOR.CONNECTOR_ID.eq(connectorId))
                            .fetchOneInto(Integer.class);

                    if (connectorPk != null && connectorPk != 0 && connectorId != 0) {
                        OutboundHttp.submit("billing-transaction-initial-connectorid", () -> {
                            log.info("connectorId: " + connectorId);
                            log.info("connectorPk: " + connectorPk);
                            log.info("insert connector");

                            String jsonInputString = String.format(
                                    "{\"chargebox\":\"%s\", \"connector\":%d, \"connectorGun\":%d}",
                                    chargeBoxIdentity, connectorPk, connectorId);
                            String endpoint = BILLING_API_BASE_URL + "transaction/initial/connectorid";
                            log.info("Endpoint: " + endpoint);
                            log.info("jsonInputString: " + jsonInputString);
                            OutboundHttp.postJson(endpoint, jsonInputString);
                        });
                    }
                }
            } else {
                log.info("The connector {}/{} already exists with connector_pk: {}.", chargeBoxIdentity, connectorId,
                        existingConnectorPk);
            }
        });
    }

    /**
     * Use case: An offline charging station decides to allow an unknown idTag to
     * start a transaction. Later, when it
     * is online, it sends a StartTransactionRequest with this idTag. If we do not
     * insert this idTag, the transaction
     * details will not be inserted into DB and we will lose valuable information.
     */
    private boolean insertIgnoreIdTag(DSLContext ctx, InsertTransactionParams p) {
        String note = "This unknown idTag was used in a transaction that started @ " + p.getStartTimestamp()
                + ". It was reported @ " + DateTime.now() + ".";

        int count = ctx.insertInto(OCPP_TAG)
                .set(OCPP_TAG.ID_TAG, p.getIdTag())
                .set(OCPP_TAG.NOTE, note)
                .set(OCPP_TAG.MAX_ACTIVE_TRANSACTION_COUNT, 0)
                .onDuplicateKeyIgnore() // Important detail
                .execute();

        return count == 1;
    }

    private boolean shouldInsertConnectorStatusAfterTransactionMsg(String chargeBoxId) {
        Record1<Integer> r = ctx.selectOne()
                .from(CHARGE_BOX)
                .where(CHARGE_BOX.CHARGE_BOX_ID.eq(chargeBoxId))
                .and(CHARGE_BOX.INSERT_CONNECTOR_STATUS_AFTER_TRANSACTION_MSG.isTrue())
                .fetchOne();

        return (r != null) && (r.value1() == 1);
    }

    private int getConnectorPkFromConnector(DSLContext ctx, String chargeBoxIdentity, int connectorId) {
        return ctx.select(CONNECTOR.CONNECTOR_PK)
                .from(CONNECTOR)
                .where(CONNECTOR.CHARGE_BOX_ID.equal(chargeBoxIdentity))
                .and(CONNECTOR.CONNECTOR_ID.equal(connectorId))
                .fetchOne()
                .value1();
    }

    private void batchInsertMeterValues(DSLContext ctx, List<MeterValue> list, int connectorPk, Integer transactionId) {
        List<ConnectorMeterValueRecord> batch = list.stream()
                .flatMap(t -> t.getSampledValue()
                        .stream()
                        .map(k -> ctx.newRecord(CONNECTOR_METER_VALUE)
                                .setConnectorPk(connectorPk)
                                .setTransactionPk(transactionId)
                                .setValueTimestamp(t.getTimestamp())
                                .setValue(k.getValue())
                                // The following are optional fields!
                                .setReadingContext(k.isSetContext() ? k.getContext().value() : null)
                                .setFormat(k.isSetFormat() ? k.getFormat().value() : null)
                                .setMeasurand(k.isSetMeasurand() ? k.getMeasurand().value() : null)
                                .setLocation(k.isSetLocation() ? k.getLocation().value() : null)
                                .setUnit(k.isSetUnit() ? k.getUnit().value() : null)
                                .setPhase(k.isSetPhase() ? k.getPhase().value() : null)))
                .collect(Collectors.toList());

        ctx.batchInsert(batch).execute();
    }

    private void tryInsertingFailed(UpdateTransactionParams p, Exception e) {
        try {
            ctx.insertInto(TRANSACTION_STOP_FAILED)
                    .set(TRANSACTION_STOP_FAILED.TRANSACTION_PK, p.getTransactionId())
                    .set(TRANSACTION_STOP_FAILED.EVENT_TIMESTAMP, p.getEventTimestamp())
                    .set(TRANSACTION_STOP_FAILED.EVENT_ACTOR, mapActor(p.getEventActor()))
                    .set(TRANSACTION_STOP_FAILED.STOP_TIMESTAMP, p.getStopTimestamp())
                    .set(TRANSACTION_STOP_FAILED.STOP_VALUE, p.getStopMeterValue())
                    .set(TRANSACTION_STOP_FAILED.STOP_REASON, p.getStopReason())
                    .set(TRANSACTION_STOP_FAILED.FAIL_REASON, Throwables.getStackTraceAsString(e))
                    .execute();
        } catch (Exception ex) {
            // This is where we give up and just log
            log.error("Exception occurred", e);
        }
    }

    private static TransactionStopFailedEventActor mapActor(TransactionStopEventActor a) {
        for (TransactionStopFailedEventActor b : TransactionStopFailedEventActor.values()) {
            if (b.getLiteral().equalsIgnoreCase(a.getLiteral())) {
                return b;
            }
        }
        // if unknown, do not throw exceptions. just insert manual.
        return TransactionStopFailedEventActor.manual;
    }
}
