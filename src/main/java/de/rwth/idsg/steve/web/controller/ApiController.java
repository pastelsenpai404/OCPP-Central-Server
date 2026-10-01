package de.rwth.idsg.steve.web.controller;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import de.rwth.idsg.steve.ocpp.CommunicationTask;
import de.rwth.idsg.steve.ocpp.OcppTransport;
import de.rwth.idsg.steve.ocpp.RequestResult;
import de.rwth.idsg.steve.repository.*;
import de.rwth.idsg.steve.repository.dto.*;
import de.rwth.idsg.steve.service.ChargePointHelperService;
import de.rwth.idsg.steve.service.ChargePointService16_Client;
import de.rwth.idsg.steve.service.TransactionStopService;
import de.rwth.idsg.steve.utils.ConnectorStatusFilter;
import de.rwth.idsg.steve.web.dto.OcppTagForm;
import de.rwth.idsg.steve.web.dto.OcppTagQueryForm;
import de.rwth.idsg.steve.web.dto.TransactionQueryForm;
import de.rwth.idsg.steve.web.dto.UserQueryForm;
import de.rwth.idsg.steve.web.dto.ocpp.ChangeConfigurationParams;
import de.rwth.idsg.steve.web.dto.ocpp.RemoteStartTransactionParams;
import de.rwth.idsg.steve.web.dto.ocpp.RemoteStopTransactionParams;
import jooq.steve.db.tables.records.AddressRecord;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.*;

import javax.annotation.PostConstruct;
import javax.servlet.http.HttpServletResponse;
import java.io.IOException;
import java.util.*;
import java.util.stream.Collectors;
import java.util.stream.Stream;

@Slf4j
@RestController
@CrossOrigin
@RequestMapping(value = "/dev", produces = MediaType.APPLICATION_JSON_VALUE)
public class ApiController {
    private final String sCHARGEBOXID = "/{chargeBoxId}";
    @Autowired
    protected ChargePointHelperService chargePointHelperService;
    @Autowired
    private ChargePointRepository chargePointRepository;
    private ObjectMapper objectMapper;
    @Autowired
    @Qualifier("ChargePointService16_Client")
    private ChargePointService16_Client client16;
    @Autowired
    private TransactionRepository transactionRepository;
    @Autowired
    private TaskStore taskStore;
    @Autowired
    private TransactionStopService transactionStopService;
    @Autowired
    private OcppTagRepository ocppTagRepository;
    @Autowired
    private UserRepository userRepository;

    @PostConstruct
    private void init() {
        objectMapper = new ObjectMapper();
        objectMapper.setSerializationInclusion(JsonInclude.Include.NON_NULL);
    }

    @GetMapping(value = sCHARGEBOXID + "/connectorIds")
    public void getConnectorIds(@PathVariable("chargeBoxId") String chargeBoxId,
            HttpServletResponse response) throws IOException {
        String s = serializeArray(chargePointRepository.getNonZeroConnectorIds(chargeBoxId));
        writeOutput(response, s);
    }

     @GetMapping(value = sCHARGEBOXID + "/startSession/{idTag}/{connector}")
    public void startRemoteSession(
            @PathVariable("chargeBoxId") String chargeBoxId,
            @PathVariable("idTag") String idTag,
            @PathVariable("connector") String connectorId,
            HttpServletResponse response) throws IOException {
        try {
            // Validate connectorId
            int connectorIdInt;
            try {
                connectorIdInt = Integer.parseInt(connectorId);
            } catch (NumberFormatException e) {
                response.setStatus(HttpServletResponse.SC_BAD_REQUEST);
                writeOutput(response, "Invalid connector ID.");
                return;
            }

            // Prepare parameters for remote start
            RemoteStartTransactionParams params = new RemoteStartTransactionParams();
            params.setIdTag(idTag);
            params.setConnectorId(connectorIdInt);

            // Set up charge point selection
            List<ChargePointSelect> chargePointSelectList = new ArrayList<>();
            ChargePointSelect chargePointSelect = new ChargePointSelect(OcppTransport.JSON, chargeBoxId);
            chargePointSelectList.add(chargePointSelect);
            params.setChargePointSelectList(chargePointSelectList);

            // Initiate remote start transaction
            CommunicationTask task = taskStore.get(client16.remoteStartTransaction(params));

            // Check if task is null
            if (task == null) {
                response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
                writeOutput(response, "Failed to initiate communication with the chargebox.");
                return;
            }

            // Introduce timeout mechanism
            long startTime = System.currentTimeMillis();
            long timeout = 30 * 1000; // 30 seconds timeout

            while (!task.isFinished()) {
                if (System.currentTimeMillis() - startTime > timeout) {
                    response.setStatus(HttpServletResponse.SC_REQUEST_TIMEOUT);
                    writeOutput(response, "Operation timed out. Chargebox might be disconnected.");
                    return;
                }
                try {
                    Thread.sleep(100); // Sleep to prevent high CPU usage
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt(); // Restore interrupted status
                    response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
                    writeOutput(response, "Thread was interrupted.");
                    return;
                }
            }

            // Retrieve the result
            RequestResult result = (RequestResult) task.getResultMap().get(chargeBoxId);
            if (result == null || result.getResponse() == null) {
                response.setStatus(HttpServletResponse.SC_PRECONDITION_FAILED);
                writeOutput(response, "No response from chargebox.");
                return;
            }

            // Handle the response
            String responseStatus = result.getResponse().toString();
            if (!"Accepted".equals(responseStatus)) {
                response.setStatus(HttpServletResponse.SC_FORBIDDEN);
                writeOutput(response, objectMapper.writeValueAsString(result.getResponse()));
            } else {
                response.setStatus(HttpServletResponse.SC_OK);
                writeOutput(response, objectMapper.writeValueAsString(result.getResponse()));
            }

        } catch (IOException e) {
            response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
            writeOutput(response, "I/O error occurred: " + e.getMessage());
        } catch (Exception e) {
            response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
            writeOutput(response, "An error occurred: " + e.getMessage());
        }
    }

    @GetMapping(value = sCHARGEBOXID + "/stopSession/{ocpp_parent}")
    public void stopRemoteSession(@PathVariable("chargeBoxId") String chargeBoxId,
            @PathVariable("ocpp_parent") String ocpp_parent,
            HttpServletResponse response) throws IOException {
        try {
            RemoteStopTransactionParams params = new RemoteStopTransactionParams();
            List<Integer> transactionIDs = transactionRepository.getActiveTransactionIds(chargeBoxId);
            if (transactionIDs.size() > 0) {
                List<String> tokenList = new ArrayList<>();
                getTokenList(ocpp_parent).forEach(token -> tokenList.add(token.get(0)));
                if (tokenList.contains(transactionRepository.getDetails(transactionIDs.get(transactionIDs.size() - 1))
                        .getTransaction().getOcppIdTag())) {
                    params.setTransactionId(transactionIDs.get(transactionIDs.size() - 1));
                    List<ChargePointSelect> cp = new ArrayList<>();
                    ChargePointSelect cps = new ChargePointSelect(OcppTransport.JSON, chargeBoxId);
                    cp.add(cps);
                    params.setChargePointSelectList(cp);
                    CommunicationTask task = taskStore.get(client16.remoteStopTransaction(params));
                    if (task == null) {
                        response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
                        writeOutput(response, "Failed to initiate communication with the chargebox.");
                        return;
                    }

                    long startTime = System.currentTimeMillis();
                    long timeout = 30 * 1000;
                    while (!task.isFinished()) {
                        if (System.currentTimeMillis() - startTime > timeout) {
                            response.setStatus(HttpServletResponse.SC_REQUEST_TIMEOUT);
                            writeOutput(response, "Operation timed out. Chargebox might be disconnected.");
                            return;
                        }
                        try {
                            Thread.sleep(100);
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
                            writeOutput(response, "Thread was interrupted.");
                            return;
                        }
                    }

                    RequestResult result = (RequestResult) task.getResultMap().get(chargeBoxId);
                    if (result == null || result.getResponse() == null) {
                        response.setStatus(HttpServletResponse.SC_PRECONDITION_FAILED);
                        writeOutput(response, "No response from chargebox.");
                        return;
                    }

                    transactionStopService.stop(transactionIDs);
                    writeOutput(response, objectMapper.writeValueAsString(result.getResponse()));
                } else {
                    response.setStatus(HttpServletResponse.SC_FORBIDDEN);
                    writeOutput(response, objectMapper.writeValueAsString("Not your charging session"));
                }
            } else {
                response.setStatus(HttpServletResponse.SC_CONFLICT);
                response.setHeader("Access-Control-Allow-Origin", "*");
            }
        } catch (NullPointerException nullPointerException) {
            response.setStatus(HttpServletResponse.SC_FORBIDDEN);
        }
    }

    @GetMapping(value = "/stopSessionByTransactionId/{transactionId}")
    public void stopRemoteSessionByTransactionId(
            @PathVariable("transactionId") Integer transactionId,
            HttpServletResponse response) throws IOException {
        try {
            if (transactionId == null || transactionId <= 0) {
                response.setStatus(HttpServletResponse.SC_BAD_REQUEST);
                writeOutput(response, "Invalid transaction ID.");
                return;
            }

            TransactionDetails details = transactionRepository.getDetails(transactionId);
            if (details == null || details.getTransaction() == null) {
                response.setStatus(HttpServletResponse.SC_NOT_FOUND);
                writeOutput(response, "Transaction not found.");
                return;
            }

            Transaction transaction = details.getTransaction();
            if (transaction.getStopTimestamp() != null && transaction.getStopValue() != null) {
                response.setStatus(HttpServletResponse.SC_CONFLICT);
                writeOutput(response, "Transaction is already stopped.");
                return;
            }

            String chargeBoxId = transaction.getChargeBoxId();
            RemoteStopTransactionParams params = new RemoteStopTransactionParams();
            params.setTransactionId(transactionId);

            List<ChargePointSelect> cp = new ArrayList<>();
            cp.add(new ChargePointSelect(OcppTransport.JSON, chargeBoxId));
            params.setChargePointSelectList(cp);

            CommunicationTask task = taskStore.get(client16.remoteStopTransaction(params));
            if (task == null) {
                response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
                writeOutput(response, "Failed to initiate communication with the chargebox.");
                return;
            }

            long startTime = System.currentTimeMillis();
            long timeout = 30 * 1000;
            while (!task.isFinished()) {
                if (System.currentTimeMillis() - startTime > timeout) {
                    response.setStatus(HttpServletResponse.SC_REQUEST_TIMEOUT);
                    writeOutput(response, "Operation timed out. Chargebox might be disconnected.");
                    return;
                }
                try {
                    Thread.sleep(100);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
                    writeOutput(response, "Thread was interrupted.");
                    return;
                }
            }

            RequestResult result = (RequestResult) task.getResultMap().get(chargeBoxId);
            if (result == null || result.getResponse() == null) {
                response.setStatus(HttpServletResponse.SC_PRECONDITION_FAILED);
                writeOutput(response, "No response from chargebox.");
                return;
            }

            String responseStatus = result.getResponse().toString();
            if (!"Accepted".equals(responseStatus)) {
                response.setStatus(HttpServletResponse.SC_FORBIDDEN);
                writeOutput(response, objectMapper.writeValueAsString(result.getResponse()));
                return;
            }

            transactionStopService.stop(transactionId);
            response.setStatus(HttpServletResponse.SC_OK);
            writeOutput(response, objectMapper.writeValueAsString(result.getResponse()));
        } catch (NullPointerException nullPointerException) {
            response.setStatus(HttpServletResponse.SC_NOT_FOUND);
            writeOutput(response, "Transaction not found.");
        } catch (IOException e) {
            response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
            writeOutput(response, "I/O error occurred: " + e.getMessage());
        } catch (Exception e) {
            response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
            writeOutput(response, "An error occurred: " + e.getMessage());
        }
    }

    @GetMapping(value = sCHARGEBOXID + "/changeConfiguration/{key}/{value}")
    public void changeConfigurationOCPP(
            @PathVariable("chargeBoxId") String chargeBoxId,
            @PathVariable("key") String key,
            @PathVariable("value") String ocppValue,
            HttpServletResponse response) throws IOException {
        try {
            // Validate input parameters
            if (chargeBoxId == null || chargeBoxId.isEmpty()) {
                response.setStatus(HttpServletResponse.SC_BAD_REQUEST);
                writeOutput(response, "Invalid chargeBoxId.");
                return;
            }

            if (key == null || key.isEmpty()) {
                response.setStatus(HttpServletResponse.SC_BAD_REQUEST);
                writeOutput(response, "Invalid configuration key.");
                return;
            }

            if (ocppValue == null) {
                response.setStatus(HttpServletResponse.SC_BAD_REQUEST);
                writeOutput(response, "Configuration value cannot be null.");
                return;
            }

            // Prepare parameters for configuration change
            ChangeConfigurationParams params = new ChangeConfigurationParams();

            List<ChargePointSelect> chargePointSelectList = new ArrayList<>();
            ChargePointSelect chargePointSelect = new ChargePointSelect(OcppTransport.JSON, chargeBoxId);
            chargePointSelectList.add(chargePointSelect);
            params.setChargePointSelectList(chargePointSelectList);
            params.setKeyType(ChangeConfigurationParams.ConfigurationKeyType.PREDEFINED);
            params.setConfKey(key);
            params.setValue(ocppValue);

            // Initiate change configuration task
            CommunicationTask task = taskStore.get(client16.changeConfiguration(params));

            // Check if task is null
            if (task == null) {
                response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
                writeOutput(response, "Failed to initiate communication with the chargebox.");
                return;
            }

            // Introduce timeout mechanism
            long startTime = System.currentTimeMillis();
            long timeout = 30 * 1000; // 30 seconds timeout

            while (!task.isFinished()) {
                if (System.currentTimeMillis() - startTime > timeout) {
                    response.setStatus(HttpServletResponse.SC_REQUEST_TIMEOUT);
                    writeOutput(response, "Operation timed out. Chargebox might be disconnected.");
                    return;
                }
                try {
                    Thread.sleep(100); // Sleep to prevent high CPU usage
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt(); // Restore interrupted status
                    response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
                    writeOutput(response, "Thread was interrupted.");
                    return;
                }
            }

            // Retrieve the result
            RequestResult result = (RequestResult) task.getResultMap().get(chargeBoxId);
            if (result == null || result.getResponse() == null) {
                response.setStatus(HttpServletResponse.SC_PRECONDITION_FAILED);
                writeOutput(response, "No response from chargebox.");
                return;
            }

            // Handle the response
            String responseStatus = result.getResponse().toString();
            if (!"Accepted".equals(responseStatus)) {
                response.setStatus(HttpServletResponse.SC_FORBIDDEN);
                writeOutput(response, objectMapper.writeValueAsString(result.getResponse()));
            } else {
                response.setStatus(HttpServletResponse.SC_OK);
                writeOutput(response, objectMapper.writeValueAsString(result.getResponse()));
            }

        } catch (IOException e) {
            response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
            writeOutput(response, "I/O error occurred: " + e.getMessage());
        } catch (Exception e) {
            response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
            writeOutput(response, "An error occurred: " + e.getMessage());
        }
    }

    @GetMapping("/apitesting")
    public void apiTest(HttpServletResponse response) throws IOException {
        String responseString = "API Test Endpoint";
        response.setContentType("text/plain");
        response.getWriter().write(responseString);
    }

    @GetMapping(value = sCHARGEBOXID)
    public void getDetails(@PathVariable("chargeBoxId") String chargeBoxId,
            HttpServletResponse response) throws IOException {

        // http://localhost:5002/develop/dev/apitest
        // /{chargeBoxId}----apitest
        // String responseString = sCHARGEBOXID + "----" + chargeBoxId;
        // response.setContentType("text/plain");
        // response.getWriter().write(responseString);
        // return;

        List<String> boxList = new ArrayList<>();
        boxList.add(chargeBoxId);

        // System.out.println("boxList Details: " + boxList);

        try {
            ChargePoint.Details cp = chargePointRepository.getDetails(
                    chargePointRepository.getChargeBoxIdPkPair(boxList).get(chargeBoxId));

            // Print directly to the console
            // System.out.println("ChargePoint Details: " + cp);

            List<Object> cbDetails = new ArrayList<>();
            cbDetails.add(cp.getChargeBox().getChargeBoxId());
            cbDetails.add(cp.getChargeBox().getChargePointVendor());
            cbDetails.add(cp.getChargeBox().getChargePointModel());
            cbDetails.add(cp.getChargeBox().getChargeBoxSerialNumber());

            cbDetails.add(cp.getChargeBox().getMeterType());
            cbDetails.add(cp.getChargeBox().getMeterSerialNumber());

            cbDetails.add(cp.getChargeBox().getNote());
            cbDetails.add(cp.getChargeBox().getDescription());

            cbDetails.add(cp.getChargeBox().getLocationLatitude());
            cbDetails.add(cp.getChargeBox().getLocationLongitude());

            // System.out.println("11cbDetails Details: " + cbDetails);

            AddressRecord addressRecord = cp.getAddress();
            if (addressRecord != null) {
                String address = addressRecord.getStreet() + " " + addressRecord.getHouseNumber() + ", "
                        + addressRecord.getCountry() + " " + addressRecord.getZipCode() + " " + addressRecord.getCity();
                cbDetails.add(address);
            } else {
                // Handle the case where the addressRecord is null
                // System.out.println("No address record found for chargeBoxId: " + chargeBoxId);
                cbDetails.add("Unknown Address"); // You can choose how you want to handle this scenario
            }

            // System.out.println("22cbDetails Details: " + cbDetails);

            List<ConnectorStatus> latestList = chargePointRepository.getChargePointConnectorStatus();
            List<ConnectorStatus> filteredList = ConnectorStatusFilter.filterAndPreferZero(latestList);

            // System.out.println("33cbDetails Details: " + cbDetails);

            // cbDetails.add(filteredList.stream()
            // .parallel()
            // .filter(cs -> chargeBoxId.equals(cs.getChargeBoxId()))
            // .findAny()
            // .orElse(null).getStatus());
            // cbDetails.add(cp.getChargeBox().getLastHeartbeatTimestamp().getMillis());

            ConnectorStatus matchedStatus = filteredList.stream()
                    .parallel()
                    .filter(cs -> chargeBoxId.equals(cs.getChargeBoxId()))
                    .findAny()
                    .orElse(null);

            if (matchedStatus != null) {
                cbDetails.add(matchedStatus.getStatus());
            } else {
                // Handle the case where no matched status is found for the chargeBoxId
                // System.out.println("No matched status found for chargeBoxId: " + chargeBoxId);
                cbDetails.add("Unknown Status"); // You can choose how you want to handle this scenario
            }

            // System.out.println("44cbDetails Details: " + cbDetails);

            writeOutput(response, serializeArray(cbDetails));
        } catch (NullPointerException nullPointerException) {
            response.setStatus(HttpServletResponse.SC_NOT_FOUND);
        }
    }

    @GetMapping
    public void getChargepoints(HttpServletResponse response) throws IOException {
        chargePointHelperService.getOcppJsonStatus();
        List<Object> cbDetails = new ArrayList<>();
        chargePointHelperService.getOcppJsonStatus().forEach(js -> {
            List<String> tmpList = new ArrayList<>();
            tmpList.add(js.getChargeBoxId());
            List<ConnectorStatus> latestList = chargePointRepository.getChargePointConnectorStatus();
            List<ConnectorStatus> filteredList = ConnectorStatusFilter.filterAndPreferZero(latestList);
            tmpList.add(filteredList
                    .stream()
                    .parallel()
                    .filter(cs -> js.getChargeBoxId().equals(cs.getChargeBoxId()))
                    .findAny()
                    .orElse(null)
                    .getStatus());
            cbDetails.add(tmpList);
        });
        String s = serializeArray(cbDetails);
        writeOutput(response, s);
    }

    // email, ocpptag = id
    @GetMapping("/user_login")
    public void getUserDetails(@RequestParam("email") String email,
            @RequestParam("id") String id,
            HttpServletResponse response) throws IOException {
        Optional<User.Overview> user = userRepository
                .getOverview(new UserQueryForm())
                .stream()
                .parallel()
                .filter(usr -> usr.getEmail().equals(email))
                .findFirst();
        // System.out.println("user Details: " + user);
        if (user.isPresent() && user.get().getOcppIdTag().equals(id)) {
            // System.out.println("user tag: " + user.get().getOcppIdTag().equals(id));
            String s = serializeArray("true");
            writeOutput(response, s);
        } else {
            // System.out.println("user tag: " + user.get().getOcppIdTag().equals(id));
            response.setStatus(HttpServletResponse.SC_FORBIDDEN);
            writeOutput(response, serializeArray("false"));
        }

    }

    @PutMapping("/addToken")
    public void putToken(@RequestParam("id") String ocpp_parent,
            @RequestParam("token") String token,
            @RequestParam(value = "note", required = false, defaultValue = " ") String note,
            HttpServletResponse response) throws IOException {
        OcppTagForm newTag = new OcppTagForm();
        newTag.setIdTag(token);
        newTag.setParentIdTag(ocpp_parent);
        if (note == null) {
            note = "";
        }
        newTag.setNote(note);
        try {
            ocppTagRepository.addOcppTag(newTag);
            writeOutput(response, serializeArray("Ok"));
        } catch (Exception exception) {
            exception.printStackTrace();
            response.setStatus(HttpServletResponse.SC_CONFLICT);
            writeOutput(response, serializeArray("Could not add new token"));
        }
    }

    @GetMapping("/getTokens")
    public void getTokens(@RequestParam("id") String ocpp_parent,
            HttpServletResponse response) throws IOException {
        try {
            List<List<String>> responseList = getTokenList(ocpp_parent);
            writeOutput(response, serializeArray(responseList));
        } catch (NullPointerException nullPointerException) {
            response.setStatus(HttpServletResponse.SC_NOT_FOUND);
        }
    }

    @DeleteMapping("/removeToken")
    public void removeToken(@RequestParam("tokenID") String token,
            HttpServletResponse response) throws IOException {
        Optional<OcppTag.Overview> ocppTag = ocppTagRepository
                .getOverview(new OcppTagQueryForm())
                .stream()
                .filter(o -> o.getIdTag().equals(token))
                .findFirst();
        // Only delete non parent ID tags
        if (ocppTag.isPresent() && ocppTag.get().getParentOcppTagPk() != null) {
            int ocppTagPk = ocppTag.get().getOcppTagPk();
            ocppTagRepository.deleteOcppTag(ocppTagPk);
            writeOutput(response, serializeArray("Ok, deleted."));
        } else {
            response.setStatus(HttpServletResponse.SC_BAD_REQUEST);
            writeOutput(response, serializeArray("Can't delete token."));
        }

    }

    // @GetMapping("/getStatistics")
    // public void getStatistics(@RequestParam("tokenID") String token,
    // @RequestParam("period") TransactionQueryForm.QueryPeriodType period
    // , @RequestParam(value = "allStatistics", required = false, defaultValue =
    // "false") boolean allStatistics,
    // HttpServletResponse response) throws IOException {
    // try {
    // TransactionQueryForm params = new TransactionQueryForm();
    // params.setPeriodType(period);
    // params.setType(TransactionQueryForm.QueryType.ALL);
    // List<String> ocppTagList = new ArrayList<>();
    // if (allStatistics) {
    // if (ocppTagRepository.getParentIdtag(token) != null) {
    // token = ocppTagRepository.getParentIdtag(token);
    // }
    // // Get all Transactions of the token
    // // First get all Tags
    // String finalToken = token;
    // ocppTagList = ocppTagRepository.getIdTags()
    // .stream()
    // .filter(tag -> Objects.equals(ocppTagRepository.getParentIdtag(tag),
    // finalToken))
    // .collect(Collectors.toList());
    // }
    // if (!ocppTagList.contains(token)) {
    // ocppTagList.add(0, token);
    // }
    // Map<Integer, List<String>> transactionMap = new LinkedHashMap<>();
    // for (String tag : ocppTagList) {
    // params.setOcppIdTag(tag);
    // Map<Integer, List<String>> finalTransactionMap = transactionMap;
    // transactionRepository.getTransactions(params).stream()
    // .parallel()
    // .filter(transaction -> transaction.getStopTimestampFormatted() != null &&
    // !transaction.getStopTimestampFormatted().isEmpty())
    // .forEach(transaction -> {
    // List<String> transactionDetailList = new ArrayList<>();
    // transactionDetailList.add(String.valueOf(transaction.getId()));
    // transactionDetailList.add(transaction.getChargeBoxId());
    // AddressRecord addressRecord =
    // chargePointRepository.getDetails(transaction.getChargeBoxPk()).getAddress();
    // String address = addressRecord.getStreet() + " " +
    // addressRecord.getHouseNumber() + ", "
    // + addressRecord.getCountry() + " " + addressRecord.getZipCode() + " " +
    // addressRecord.getCity();
    // transactionDetailList.add(address);
    // transactionDetailList.add(transaction.getOcppIdTag());
    // transactionDetailList.add(transaction.getStartValue());
    // transactionDetailList.add(transaction.getStartTimestampFormatted().toString());
    // transactionDetailList.add(transaction.getStopTimestampFormatted().toString());
    // transactionDetailList.add(transaction.getStopReason());
    // transactionDetailList.add(transaction.getStopEventActor().toString());
    // transactionDetailList.add(transaction.getStopValue());
    // transactionRepository.getDetails(transaction
    // .getId())
    // .getValues()
    // .stream()
    // .parallel()
    // .filter(meterValues -> meterValues.getUnit() != null &&
    // !meterValues.getUnit().isEmpty())
    // .findFirst()
    // .ifPresentOrElse((meterValues ->
    // transactionDetailList.add(meterValues.getUnit())), () ->
    // transactionDetailList.add(""));
    // finalTransactionMap.put(transaction.getId(), transactionDetailList);
    // });
    // }
    // transactionMap = transactionMap.entrySet()
    // .stream()
    // .parallel()
    // .sorted(Collections.reverseOrder(Map.Entry.comparingByKey()))
    // .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue,
    // (e1, e2) -> e2, LinkedHashMap::new));
    // writeOutput(response, serializeArray(transactionMap));
    // } catch (NullPointerException nullPointerException) {
    // response.setStatus(HttpServletResponse.SC_NOT_FOUND);
    // } catch (Exception illegalArgumentException) {
    // response.setStatus(HttpServletResponse.SC_BAD_REQUEST);
    // }
    // }

    @RequestMapping(method = RequestMethod.OPTIONS, value = "/**")
    public void manageOptions(HttpServletResponse response) throws IOException {
        writeOutput(response, "");
    }

    private List<List<String>> getTokenList(String ocpp_parent) throws NullPointerException {
        List<String> ocppTagList = ocppTagRepository.getIdTags()
                .stream()
                .filter(tag -> Objects.equals(ocppTagRepository.getParentIdtag(tag), ocpp_parent))
                .collect(Collectors.toList());
        if (!ocppTagList.contains(ocpp_parent)) {
            ocppTagList.add(0, ocpp_parent);
        }
        List<List<String>> responseList = new ArrayList<>();

        ocppTagList.forEach(tag -> {
            OcppTagQueryForm ocppTagQueryForm = new OcppTagQueryForm();
            ocppTagQueryForm.setIdTag(tag);
            String note;
            Optional<OcppTag.Overview> optionalOverview = ocppTagRepository.getOverview(ocppTagQueryForm).stream()
                    .findFirst();
            if (optionalOverview.isPresent()) {
                note = ocppTagRepository.getRecord(optionalOverview.get().getOcppTagPk()).getNote();
                if (note == null) {
                    note = "";
                }
                responseList.add(Stream.of(tag, note).collect(Collectors.toList()));
            } else {
                throw new NullPointerException();
            }

        });
        return responseList;
    }

    private String serializeArray(Object object) {
        try {
            return objectMapper.writeValueAsString(object);
        } catch (JsonProcessingException e) {
            // As fallback return empty array, do not let the frontend hang
            log.error("Error occurred during serialization of response. Returning empty array instead!", e);
            return "[]";
        }
    }

    /**
     * We want to handle this JSON conversion locally, and do not want to register
     * an application-wide
     * HttpMessageConverter just for this little class. Otherwise, it might have
     * unwanted side effects due to
     * different serialization/deserialization needs of different APIs.
     * <p>
     * That's why we are directly accessing the low-level HttpServletResponse and
     * manually writing to output.
     */
    private void writeOutput(HttpServletResponse response, String str) throws IOException {
        response.setContentType(MediaType.APPLICATION_JSON_VALUE);
        response.setHeader("Access-Control-Allow-Origin", "*");
        response.setHeader("Access-Control-Allow-Methods", "POST, GET, PUT, DELETE, OPTIONS");
        response.setHeader("Access-Control-Allow-Headers", "Content-Type");
        response.getWriter().write(str);
    }

}
