package de.rwth.idsg.steve.utils.mapper;

import de.rwth.idsg.steve.utils.ControllerHelper;
import de.rwth.idsg.steve.web.dto.OcppTagForm;
import jooq.steve.db.tables.records.OcppTagActivityRecord;
import lombok.AccessLevel;
import lombok.NoArgsConstructor;
import org.joda.time.DateTime;

@NoArgsConstructor(access = AccessLevel.PRIVATE)
public final class OcppTagFormMapper {

    public static OcppTagForm toForm(OcppTagActivityRecord record) {
        OcppTagForm form = new OcppTagForm();
        form.setOcppTagPk(record.getOcppTagPk());
        form.setIdTag(record.getIdTag());

        DateTime expiryDate = record.getExpiryDate();
        if (expiryDate != null) {
            form.setExpiryDate(expiryDate.toLocalDateTime());
        }

        form.setMaxActiveTransactionCount(record.getMaxActiveTransactionCount());
        form.setNote(record.getNote());

        String parentIdTag = record.getParentIdTag();
        if (parentIdTag == null) {
            parentIdTag = ControllerHelper.EMPTY_OPTION;
        }
        form.setParentIdTag(parentIdTag);

        return form;
    }
}
