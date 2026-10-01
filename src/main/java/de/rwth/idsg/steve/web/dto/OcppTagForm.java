package de.rwth.idsg.steve.web.dto;

import de.rwth.idsg.steve.web.validation.IdTag;
import io.swagger.annotations.ApiModelProperty;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import org.joda.time.LocalDateTime;

import javax.validation.constraints.Future;
import javax.validation.constraints.NotEmpty;
import java.util.Objects;

@Getter
@Setter
@ToString
@EqualsAndHashCode
public class OcppTagForm {

    // Internal database id
    @ApiModelProperty(hidden = true)
    private Integer ocppTagPk;

    @ApiModelProperty(value = "Will be used in create/insert flows. Will be ignored in update flows.")
    @NotEmpty(message = "ID Tag is required")
    @IdTag
    private String idTag;

    // Is a FK in DB table. No validation needed. Operation will fail if DB constraint fails.
    private String parentIdTag;

    @ApiModelProperty(value = "A date/time without timezone. Example: 2022-10-10T09:00")
    @Future(message = "Expiry Date/Time must be in future")
    private LocalDateTime expiryDate;

    private Integer maxActiveTransactionCount;

    private String note;

    /**
     * As specified in V0_9_9__update.sql default value is 1.
     */
    public Integer getMaxActiveTransactionCount() {
        return Objects.requireNonNullElse(maxActiveTransactionCount, 1);
    }
}
