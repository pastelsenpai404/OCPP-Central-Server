package de.rwth.idsg.steve.ocpp;

import lombok.Getter;
import lombok.Setter;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

@Getter
@Setter
public class RequestResult {
    private String response;
    private String errorMessage;
    private Object details;

    @Nullable
    @SuppressWarnings("unchecked")
    public <T> T getDetails() {
        return (T) details;
    }

    public <T> void setDetails(@NotNull T item) {
        this.details = item;
    }
}
