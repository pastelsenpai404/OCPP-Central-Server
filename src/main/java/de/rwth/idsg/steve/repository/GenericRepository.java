package de.rwth.idsg.steve.repository;

import de.rwth.idsg.steve.repository.dto.DbVersion;
import de.rwth.idsg.steve.web.dto.Statistics;

public interface GenericRepository {
    Statistics getStats();

    /**
     * Returns database version of SteVe and last database update timestamp
     *
     */
    DbVersion getDBVersion();
}
