package de.rwth.idsg.steve.utils;

import lombok.AccessLevel;
import lombok.NoArgsConstructor;
import org.joda.time.DateTime;
import org.jooq.Condition;
import org.jooq.DSLContext;
import org.jooq.DatePart;
import org.jooq.Field;
import org.jooq.impl.DSL;

import java.sql.Timestamp;

import static org.jooq.impl.DSL.field;

@NoArgsConstructor(access = AccessLevel.PRIVATE)
public final class CustomDSL {

    public static Field<DateTime> date(DateTime dt) {
        return date(DSL.val(dt, DateTime.class));
    }

    public static Field<DateTime> date(Field<DateTime> dt) {
        return field("date({0})", DateTime.class, dt);
    }

    /**
     * http://dev.mysql.com/doc/refman/5.7/en/pattern-matching.html
     *
     */
    public static Condition includes(Field<String> field, String input) {

        input = input.replaceAll("\\s+", "%");

        return field.like("%" + input + "%");
    }

    public static Long selectOffsetFromUtcInSeconds(DSLContext ctx) {
        return ctx.select(timestampDiffBetweenUtcAndCurrent(DatePart.SECOND))
                  .fetchOne()
                  .getValue(0, Long.class);
    }

    private static Field<Long> timestampDiffBetweenUtcAndCurrent(DatePart part) {
        return timestampDiff(part, utcTimestamp(), DSL.currentTimestamp());
    }

    /**
     * Taken from https://github.com/jOOQ/jOOQ/issues/4303#issuecomment-105519975
     */
    private static Field<Long> timestampDiff(DatePart part, Field<Timestamp> t1, Field<Timestamp> t2) {
        return field("timestampdiff({0}, {1}, {2})", Long.class, DSL.keyword(part.toSQL()), t1, t2);
    }

    private static Field<Timestamp> utcTimestamp() {
        return field("{utc_timestamp()}", Timestamp.class);
    }
}
