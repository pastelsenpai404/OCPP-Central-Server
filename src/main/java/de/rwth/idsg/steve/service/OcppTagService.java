package de.rwth.idsg.steve.service;

import com.google.common.base.Strings;
import de.rwth.idsg.steve.SteveException;
import de.rwth.idsg.steve.repository.OcppTagRepository;
import de.rwth.idsg.steve.repository.SettingsRepository;
import de.rwth.idsg.steve.repository.dto.OcppTag;
import de.rwth.idsg.steve.service.dto.UnidentifiedIncomingObject;
import de.rwth.idsg.steve.utils.OutboundHttp;
import de.rwth.idsg.steve.web.dto.OcppTagForm;
import de.rwth.idsg.steve.web.dto.OcppTagQueryForm;
import jooq.steve.db.tables.records.OcppTagActivityRecord;
import jooq.steve.db.tables.records.OcppTagRecord;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import ocpp.cp._2015._10.AuthorizationData;
import ocpp.cs._2015._10.AuthorizationStatus;
import ocpp.cs._2015._10.IdTagInfo;
import org.jetbrains.annotations.Nullable;
import org.joda.time.DateTime;
import org.springframework.stereotype.Service;

import de.rwth.idsg.steve.ocpp.ws.data.CommunicationContext;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Collections;
import java.util.List;
import java.util.function.Supplier;

import static de.rwth.idsg.steve.SteveConfiguration.CONFIG;

@Slf4j
@Service
@RequiredArgsConstructor
public class OcppTagService {

    private static final String BILLING_API_BASE_URL = CONFIG.getBillingApi().getBaseUrl();
    private static final String BILLING_DB_URL = CONFIG.getBillingDb().getUrl();
    private static final String BILLING_DB_USER = CONFIG.getBillingDb().getUserName();
    private static final String BILLING_DB_PASSWORD = CONFIG.getBillingDb().getPassword();

    private final UnidentifiedIncomingObjectService invalidOcppTagService = new UnidentifiedIncomingObjectService(1000);

    private final SettingsRepository settingsRepository;
    private final OcppTagRepository ocppTagRepository;

    public List<OcppTag.Overview> getOverview(OcppTagQueryForm form) {
        return ocppTagRepository.getOverview(form);
    }

    public OcppTagActivityRecord getRecord(int ocppTagPk) {
        return ocppTagRepository.getRecord(ocppTagPk);
    }

    public List<String> getIdTags() {
        return ocppTagRepository.getIdTags();
    }

    public List<String> getActiveIdTags() {
        return ocppTagRepository.getActiveIdTags();
    }

    public List<String> getParentIdTags() {
        return ocppTagRepository.getParentIdTags();
    }

    public String getParentIdtag(String idTag) {
        return ocppTagRepository.getParentIdtag(idTag);
    }

    public List<AuthorizationData> getAuthDataOfAllTags() {
        DateTime nowDt = DateTime.now();
        return ocppTagRepository.getOcppTagRecords().map(record -> mapToAuthorizationData(record, nowDt));
    }

    public List<AuthorizationData> getAuthData(List<String> idTagList) {
        DateTime nowDt = DateTime.now();
        return ocppTagRepository.getOcppTagRecords(idTagList).map(record -> mapToAuthorizationData(record, nowDt));
    }

    public List<UnidentifiedIncomingObject> getUnknownOcppTags() {
        return invalidOcppTagService.getObjects();
    }

    public void removeUnknown(List<String> idTagList) {
        invalidOcppTagService.removeAll(idTagList);
    }

    @Nullable
    public IdTagInfo getIdTagInfo(@Nullable String idTag, Integer isStartTransactionReqContext,
            String chargeBoxIdentity) {
        if (Strings.isNullOrEmpty(idTag)) {
            return null;
        }
        log.info("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbcacacac The connector {} is NEW, and inserted into DB.",
                chargeBoxIdentity);
        // OcppTagActivityRecord record = ocppTagRepository.getRecord(idTag);
        OcppTagRecord record = ocppTagRepository.getOcppTagRecord(idTag);
        log.info("ggggggggggggggggggggggggggggggggggggg The connector {} is NEW, and inserted into DB.",
                chargeBoxIdentity);
        AuthorizationStatus status = decideStatus(record, idTag, isStartTransactionReqContext);

        log.info("AAAAAAAAAAAAAAAA The connector {} is NEW, and inserted into DB.", chargeBoxIdentity);

        if ((status == AuthorizationStatus.ACCEPTED || status == AuthorizationStatus.INVALID) && (isStartTransactionReqContext == 0)) {
            log.info("INSIDE LOGIC The connector {} is NEW, and inserted into DB.", chargeBoxIdentity);

            final String finalChargeBoxIdentity = chargeBoxIdentity;
            final String finalIdTag = idTag;

            OutboundHttp.submit("billing-authentication-card", () -> {
                Thread.sleep(100);

                String jsonInputString = String.format("{\"chargebox\":\"%s\", \"tag\":\"%s\"}",
                        finalChargeBoxIdentity, finalIdTag);
                String endpoint = BILLING_API_BASE_URL +
                        "transaction/authentication/card";
                log.info("Endpoint: " + endpoint);
                log.info("jsonInputString: " + jsonInputString);
                OutboundHttp.postJson(endpoint, jsonInputString);
            });

            // Queries to get IDs and check conditions
            final String getChargeboxQuery = "SELECT id, brand FROM chargebox WHERE `chargebox-id` = ?";
            final String checkSchneiderEventQuery = "SELECT COUNT(*) AS eventCount FROM `schneider-event` WHERE chargebox = ? AND details = 'start'";

            // Initialize variables
            Connection connectionBilling = null;
            PreparedStatement stmt = null;
            ResultSet rs = null;
            boolean autorizeStatus = false;

            try {
                // Open connection
                connectionBilling = DriverManager.getConnection(BILLING_DB_URL, BILLING_DB_USER, BILLING_DB_PASSWORD);

                // Get chargebox ID and brand
                stmt = connectionBilling.prepareStatement(getChargeboxQuery);
                stmt.setString(1, finalChargeBoxIdentity);
                rs = stmt.executeQuery();
                int chargeboxId = 0;
                int brand = 0;
                if (rs.next()) {
                    chargeboxId = rs.getInt("id");
                    brand = rs.getInt("brand");
                }
                stmt.close();
                rs.close();

                // Check if brand is 3
                if (brand == 3) {
                    // Check schneider-event for start details
                    stmt = connectionBilling.prepareStatement(checkSchneiderEventQuery);
                    stmt.setInt(1, chargeboxId);
                    rs = stmt.executeQuery();
                    if (rs.next() && rs.getInt("eventCount") > 0) {
                        autorizeStatus = true; // Accept the status
                        final int finalchargeboxIdfromsql = chargeboxId;
                        OutboundHttp.submit("billing-schneider-clear-start", () -> {
                            Thread.sleep(100);

                            String jsonInputString = String.format("{\"chargebox\":%d}",
                                    finalchargeboxIdfromsql);
                            String endpoint = BILLING_API_BASE_URL +
                                    "transaction/schneider/clear-start";
                            log.info("Endpoint: " + endpoint);
                            log.info("jsonInputString: " + jsonInputString);
                            OutboundHttp.postJson(endpoint, jsonInputString);
                        });
                    }
                    stmt.close();
                    rs.close();
                }
            } catch (SQLException e) {
                // Handle SQL Exception
                log.error("SQL Error: " + e.getMessage(), e);
            } finally {
                // Close resources
                try {
                    if (stmt != null)
                        stmt.close();
                    if (rs != null)
                        rs.close();
                    if (connectionBilling != null)
                        connectionBilling.close();
                } catch (SQLException e) {
                    log.error("SQL Error during closing resources: " + e.getMessage(), e);
                }
            }

            // Use autorizeStatus as needed
            if (autorizeStatus) {
                // Proceed with accepted status
                // System.out.println("Autorize Status: accept");
                status = AuthorizationStatus.ACCEPTED;
            } else {
                // Handle other cases
                // System.out.println("Autorize Status: reject");
                status = AuthorizationStatus.INVALID;
            }

        }

        switch (status) {
            case INVALID:
                invalidOcppTagService.processNewUnidentified(idTag);
                return new IdTagInfo().withStatus(status);

            case BLOCKED:
            case EXPIRED:
            case CONCURRENT_TX:
            case ACCEPTED:
                return new IdTagInfo().withStatus(status)
                        .withParentIdTag(record.getParentIdTag())
                        .withExpiryDate(getExpiryDateOrDefault(record));
            default:
                throw new SteveException("Unexpected AuthorizationStatus");
        }
    }

    @Nullable
    public IdTagInfo getIdTagInfo(@Nullable String idTag, Integer isStartTransactionReqContext,
            Supplier<IdTagInfo> supplierWhenException, String chargeBoxIdentity) {
        try {
            return getIdTagInfo(idTag, isStartTransactionReqContext, chargeBoxIdentity);
        } catch (Exception e) {
            log.error("Exception occurred", e);
            return supplierWhenException.get();
        }
    }

    // -------------------------------------------------------------------------
    // Create, Update, Delete operations
    // -------------------------------------------------------------------------

    public int addOcppTag(OcppTagForm form) {
        var id = ocppTagRepository.addOcppTag(form);
        removeUnknown(Collections.singletonList(form.getIdTag()));
        return id;
    }

    public void addOcppTagList(List<String> idTagList) {
        ocppTagRepository.addOcppTagList(idTagList);
        removeUnknown(idTagList);
    }

    public void updateOcppTag(OcppTagForm form) {
        ocppTagRepository.updateOcppTag(form);
    }

    public void deleteOcppTag(int ocppTagPk) {
        ocppTagRepository.deleteOcppTag(ocppTagPk);
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /**
     * If the database contains an actual expiry, use it. Otherwise, calculate an
     * expiry for cached info
     */
    @Nullable
    private DateTime getExpiryDateOrDefault(OcppTagRecord record) {
        if (record.getExpiryDate() != null) {
            return record.getExpiryDate();
        }

        int hoursToExpire = settingsRepository.getHoursToExpire();

        // From web page: The value 0 disables this functionality (i.e. no expiry date
        // will be set).
        if (hoursToExpire == 0) {
            return null;
        } else {
            return DateTime.now().plusHours(hoursToExpire);
        }
    }

    private AuthorizationStatus decideStatus(OcppTagRecord record, String idTag,
            Integer isStartTransactionReqContext) {
        if (record == null) {
            log.error("The user with idTag '{}' is INVALID (not present in DB).", idTag);
            return AuthorizationStatus.INVALID;
        }

        if (isBlocked(record)) {
            log.error("The user with idTag '{}' is BLOCKED.", idTag);
            return AuthorizationStatus.BLOCKED;
        }

        if (isExpired(record, DateTime.now())) {
            log.error("The user with idTag '{}' is EXPIRED.", idTag);
            return AuthorizationStatus.EXPIRED;
        }

        // in billing system -> we dont use
        // if (isStartTransactionReqContext && reachedLimitOfActiveTransactions(record))
        // {
        // log.warn("The user with idTag '{}' is ALREADY in another transaction(s).",
        // idTag);
        // return AuthorizationStatus.CONCURRENT_TX;
        // }

        log.info("The user with idTag '{}' is ACCEPTED.", record.getIdTag());
        return AuthorizationStatus.ACCEPTED;
    }

    /**
     * ConcurrentTx is only valid for StartTransactionRequest
     */
    private static ocpp.cp._2015._10.AuthorizationStatus decideStatusForAuthData(OcppTagRecord record,
            DateTime now) {
        if (isBlocked(record)) {
            return ocpp.cp._2015._10.AuthorizationStatus.BLOCKED;
        } else if (isExpired(record, now)) {
            return ocpp.cp._2015._10.AuthorizationStatus.EXPIRED;
            // } else if (reachedLimitOfActiveTransactions(record)) {
            // return ocpp.cp._2015._10.AuthorizationStatus.CONCURRENT_TX;
        } else {
            return ocpp.cp._2015._10.AuthorizationStatus.ACCEPTED;
        }
    }

    private static boolean isExpired(OcppTagRecord record, DateTime now) {
        DateTime expiry = record.getExpiryDate();
        return expiry != null && now.isAfter(expiry);
    }

    private static boolean isBlocked(OcppTagRecord record) {
        return record.getMaxActiveTransactionCount() == 0;
    }

    // private static boolean reachedLimitOfActiveTransactions(OcppTagRecord record)
    // {
    // int max = record.getMaxActiveTransactionCount();

    // // blocked
    // if (max == 0) {
    // return true;
    // }

    // // allow all
    // if (max < 0) {
    // return false;
    // }

    // // allow as specified
    // return record.getActiveTransactionCount() >= max;
    // }

    private static AuthorizationData mapToAuthorizationData(OcppTagRecord record, DateTime nowDt) {
        return new AuthorizationData().withIdTag(record.getIdTag())
                .withIdTagInfo(
                        new ocpp.cp._2015._10.IdTagInfo()
                                .withStatus(decideStatusForAuthData(record, nowDt))
                                .withParentIdTag(record.getParentIdTag())
                                .withExpiryDate(record.getExpiryDate()));
    }
}
