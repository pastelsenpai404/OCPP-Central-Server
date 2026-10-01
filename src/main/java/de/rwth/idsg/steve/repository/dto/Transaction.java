package de.rwth.idsg.steve.repository.dto;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.swagger.annotations.ApiModelProperty;
import jooq.steve.db.enums.TransactionStopEventActor;
import lombok.Builder;
import lombok.Getter;
import lombok.ToString;
import org.jetbrains.annotations.Nullable;
import org.joda.time.DateTime;

@Getter
@Builder
@ToString
public final class Transaction {

    private final int id;
    private final int connectorId;
    private final int chargeBoxPk;
    private final int ocppTagPk;

    private final String chargeBoxId;

    private final String ocppIdTag;

    /**
     * Only relevant for the web pages. Disabled for API
     */
    @JsonIgnore
    @ApiModelProperty(hidden = true)
    private final String startTimestampFormatted;

    private final String startValue;

    private final DateTime startTimestamp;

    /**
     * Only relevant for the web pages. Disabled for API
     */
    @JsonIgnore
    @ApiModelProperty(hidden = true)
    @Nullable
    private final String stopTimestampFormatted;

    @Nullable
    private final String stopValue;

    @Nullable
    private final String stopReason; // new in OCPP 1.6

    @Nullable
    private final DateTime stopTimestamp;

    @Nullable
    private final TransactionStopEventActor stopEventActor;
}
