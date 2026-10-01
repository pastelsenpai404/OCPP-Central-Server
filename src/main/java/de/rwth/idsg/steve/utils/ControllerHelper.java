package de.rwth.idsg.steve.utils;

import lombok.AccessLevel;
import lombok.NoArgsConstructor;
import lombok.experimental.UtilityClass;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static de.rwth.idsg.steve.utils.CountryCodesProvider.getCountryCodes;

@NoArgsConstructor(access = AccessLevel.PRIVATE)
public final class ControllerHelper {

    public static final String EMPTY_OPTION = "-- Empty --";

    public static final Map<String, String> COUNTRY_DROPDOWN = getCountryCodes();

    public static Map<String, String> idTagEnhancer(List<String> idTagList) {
        Map<String, String> map = new HashMap<>(idTagList.size() + 1);
        map.put("", EMPTY_OPTION);

        for (String s : idTagList) {
            map.put(s, s);
        }
        return map;
    }


}
