package de.rwth.idsg.steve.repository.dto;

import jooq.steve.db.tables.records.AddressRecord;
import jooq.steve.db.tables.records.ChargeBoxRecord;
import lombok.Builder;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import org.joda.time.DateTime;

public final class ChargePoint {

    @Getter
    @Builder
    public static final class Overview {
        private final int chargeBoxPk;
        private final String chargeBoxId, description, ocppProtocol, lastHeartbeatTimestamp;
        private final DateTime lastHeartbeatTimestampDT;
    }

    @Getter
    @RequiredArgsConstructor
    public static final class Details {
        private final ChargeBoxRecord chargeBox;
        private final AddressRecord address;
    }

}
