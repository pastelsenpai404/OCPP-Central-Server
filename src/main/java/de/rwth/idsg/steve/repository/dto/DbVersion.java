package de.rwth.idsg.steve.repository.dto;

import lombok.Builder;
import lombok.Getter;

@Getter
@Builder
public final class DbVersion {
    private final String version, updateTimestamp;
}
