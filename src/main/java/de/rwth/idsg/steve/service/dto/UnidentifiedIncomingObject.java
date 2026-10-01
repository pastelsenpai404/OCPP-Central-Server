package de.rwth.idsg.steve.service.dto;

import lombok.Getter;
import lombok.ToString;
import org.joda.time.DateTime;

@ToString
@Getter
public class UnidentifiedIncomingObject {

    private final String key;
    private int numberOfAttempts = 0;
    private DateTime lastAttemptTimestamp;

    public UnidentifiedIncomingObject(String key) {
        this.key = key;
    }

    public synchronized void updateStats() {
        numberOfAttempts++;
        lastAttemptTimestamp = DateTime.now();
    }
}
