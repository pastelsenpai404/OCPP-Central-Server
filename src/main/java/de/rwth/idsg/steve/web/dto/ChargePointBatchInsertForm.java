package de.rwth.idsg.steve.web.dto;

import de.rwth.idsg.steve.web.validation.ChargeBoxId;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;

import javax.validation.constraints.NotEmpty;
import java.util.List;

@Getter
@Setter
@ToString
public class ChargePointBatchInsertForm {

    @NotEmpty
    @ChargeBoxId
    private List<String> idList;
}
