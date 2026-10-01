// package de.rwth.idsg.steve.repository;

// import de.rwth.idsg.steve.repository.dto.OcppTag;
// import de.rwth.idsg.steve.web.dto.OcppTagForm;
// import de.rwth.idsg.steve.web.dto.OcppTagQueryForm;
// import jooq.steve.db.tables.records.OcppTagActivityRecord;
// import org.jooq.Result;

// import java.util.List;

// public interface OcppTagRepository {
//     List<OcppTag.Overview> getOverview(OcppTagQueryForm form);

//     Result<OcppTagActivityRecord> getRecords();
//     Result<OcppTagActivityRecord> getRecords(List<String> idTagList);

//     OcppTagActivityRecord getRecord(String idTag);
//     OcppTagActivityRecord getRecord(int ocppTagPk);

//     List<String> getIdTags();
//     List<String> getActiveIdTags();

//     List<String> getParentIdTags();
//     String getParentIdtag(String idTag);

//     void addOcppTagList(List<String> idTagList);
//     int addOcppTag(OcppTagForm form);
//     void updateOcppTag(OcppTagForm form);
//     void deleteOcppTag(int ocppTagPk);
// }

package de.rwth.idsg.steve.repository;

import de.rwth.idsg.steve.repository.dto.OcppTag;
import de.rwth.idsg.steve.web.dto.OcppTagForm;
import de.rwth.idsg.steve.web.dto.OcppTagQueryForm;
import jooq.steve.db.tables.records.OcppTagActivityRecord;
import jooq.steve.db.tables.records.OcppTagRecord;
import org.jooq.Result;

import java.util.List;

public interface OcppTagRepository {
    List<OcppTag.Overview> getOverview(OcppTagQueryForm form);

    Result<OcppTagActivityRecord> getRecords();

    Result<OcppTagActivityRecord> getRecords(List<String> idTagList);

    Result<OcppTagRecord> getOcppTagRecords();

    Result<OcppTagRecord> getOcppTagRecords(List<String> idTagList);

    OcppTagActivityRecord getRecord(String idTag);

    OcppTagActivityRecord getRecord(int ocppTagPk);

    OcppTagRecord getOcppTagRecord(String idTag);

    OcppTagRecord getOcppTagRecord(int ocppTagPk);

    List<String> getIdTags();

    List<String> getActiveIdTags();

    List<String> getParentIdTags();

    String getParentIdtag(String idTag);

    void addOcppTagList(List<String> idTagList);

    int addOcppTag(OcppTagForm form);

    void updateOcppTag(OcppTagForm form);

    void deleteOcppTag(int ocppTagPk);
}
