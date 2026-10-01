package de.rwth.idsg.steve.web.dto;

import lombok.Getter;
import lombok.Setter;
import lombok.ToString;

@Getter
@Setter
@ToString
public class ChargingProfileAssignmentQueryForm {

    private String chargeBoxId;
    private Integer chargingProfilePk;
    private String chargingProfileDescription;

}
