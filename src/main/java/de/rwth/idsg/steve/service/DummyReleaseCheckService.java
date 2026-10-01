package de.rwth.idsg.steve.service;

import de.rwth.idsg.steve.web.dto.ReleaseReport;

public class DummyReleaseCheckService implements ReleaseCheckService {
    @Override
    public ReleaseReport check() {
        return new ReleaseReport(false);
    }
}
