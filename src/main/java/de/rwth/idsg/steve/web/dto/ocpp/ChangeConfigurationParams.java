package de.rwth.idsg.steve.web.dto.ocpp;

import com.google.common.base.Strings;
import de.rwth.idsg.steve.SteveException;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.Setter;

import javax.validation.constraints.AssertTrue;
import javax.validation.constraints.NotNull;
import java.util.Objects;

@Getter
@Setter
public class ChangeConfigurationParams extends MultipleChargePointSelect {

    private String confKey;

    private String customConfKey;

    @NotNull(message = "Key type is required")
    private ConfigurationKeyType keyType = ConfigurationKeyType.PREDEFINED;

    // @NotBlank(message = "Value is required")
    // @Pattern(regexp = "\\S+", message = "Value cannot contain any whitespace")
    private String value;

    @AssertTrue(message = "Custom Configuration Key cannot be left blank")
    public boolean isValidCustom() {
        if (keyType == ConfigurationKeyType.CUSTOM) {
            return !Strings.isNullOrEmpty(customConfKey);
        } else {
            return true;
        }
    }

    @AssertTrue(message = "Configuration Key is required")
    public boolean isValidPredefined() {
        if (keyType == ConfigurationKeyType.PREDEFINED) {
            return confKey != null;
        } else {
            return true;
        }
    }

    public String getKey() {
        if (keyType == ConfigurationKeyType.PREDEFINED) {
            return confKey;
        } else if (keyType == ConfigurationKeyType.CUSTOM) {
            return customConfKey;
        }

        // This should not happen
        throw new SteveException("Cannot determine key (KeyType in illegal state)");
    }

    public String getValue() {
        return Objects.requireNonNullElse(value, "");
    }

    // -------------------------------------------------------------------------
    // Enum
    // -------------------------------------------------------------------------

    @RequiredArgsConstructor
    public enum ConfigurationKeyType {
        PREDEFINED("Predefined"),
        CUSTOM("Custom");

        @Getter private final String value;

        public static ConfigurationKeyType fromValue(String v) {
            for (ConfigurationKeyType c : ConfigurationKeyType.values()) {
                if (c.value.equals(v)) {
                    return c;
                }
            }
            throw new IllegalArgumentException(v);
        }
    }

}