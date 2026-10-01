package de.rwth.idsg.steve.web.dto;

import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import ocpp.cp._2015._10.ChargingProfileKindType;
import ocpp.cp._2015._10.ChargingProfilePurposeType;
import ocpp.cp._2015._10.RecurrencyKindType;
import org.joda.time.LocalDateTime;

@Getter
@Setter
@ToString
public class ChargingProfileQueryForm {
    private Integer chargingProfilePk;
    private Integer stackLevel;
    private String description;
    private ChargingProfilePurposeType profilePurpose;
    private ChargingProfileKindType profileKind;
    private RecurrencyKindType recurrencyKind;
    private LocalDateTime validFrom;
    private LocalDateTime validTo;

}
