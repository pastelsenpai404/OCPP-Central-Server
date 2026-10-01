package de.rwth.idsg.steve.repository;

import de.rwth.idsg.steve.web.dto.Address;
import jooq.steve.db.tables.records.AddressRecord;
import org.jetbrains.annotations.Nullable;
import org.jooq.DSLContext;
import org.jooq.Record1;
import org.jooq.SelectConditionStep;

public interface AddressRepository {
    @Nullable AddressRecord get(DSLContext ctx, Integer addressPk);
    @Nullable Integer updateOrInsert(DSLContext ctx, Address address);
    void delete(DSLContext ctx, SelectConditionStep<Record1<Integer>> addressPkSelect);
}
