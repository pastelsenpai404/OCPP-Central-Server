package de.rwth.idsg.steve.web;

import com.google.common.base.Strings;
import lombok.AccessLevel;
import lombok.RequiredArgsConstructor;
import org.joda.time.LocalDateTime;
import org.joda.time.format.DateTimeFormat;
import org.joda.time.format.DateTimeFormatter;
import org.joda.time.format.ISODateTimeFormat;
import org.springframework.security.core.parameters.P;

import java.beans.PropertyEditorSupport;

@RequiredArgsConstructor(access = AccessLevel.PRIVATE)
public class LocalDateTimeEditor extends PropertyEditorSupport {

    private final DateTimeFormatter dateTimeFormatter;

    public static LocalDateTimeEditor forMvc() {
        return new LocalDateTimeEditor(DateTimeFormat.forPattern("yyyy-MM-dd HH:mm"));
    }

    public static LocalDateTimeEditor forApi() {
        return new LocalDateTimeEditor(ISODateTimeFormat.localDateOptionalTimeParser());
    }

    @Override
    public String getAsText() {
        Object value = getValue();
        if (value == null) {
            return null;
        } else {
            return dateTimeFormatter.print((LocalDateTime) value);
        }
    }

    @Override
    public void setAsText(String text) {
        if (Strings.isNullOrEmpty(text)) {
            setValue(null);
        } else {
            setValue(dateTimeFormatter.parseLocalDateTime(text));
        }
    }
}
