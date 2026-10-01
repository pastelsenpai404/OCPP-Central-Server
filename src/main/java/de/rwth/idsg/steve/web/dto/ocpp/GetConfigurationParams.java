package de.rwth.idsg.steve.web.dto.ocpp;

import lombok.Getter;
import lombok.Setter;

import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import static de.rwth.idsg.steve.utils.StringUtils.splitByComma;

@Setter
@Getter
public class GetConfigurationParams extends MultipleChargePointSelect {

    private List<String> confKeyList;

    private String commaSeparatedCustomConfKeys;

    public List<String> getAllKeys() {
        List<String> fromPredefined = Objects.requireNonNullElse(confKeyList, Collections.emptyList());
        List<String> fromCustom = splitByComma(commaSeparatedCustomConfKeys);

        return Stream.of(fromPredefined, fromCustom)
                     .flatMap(Collection::stream)
                     .collect(Collectors.toList());
    }
}
