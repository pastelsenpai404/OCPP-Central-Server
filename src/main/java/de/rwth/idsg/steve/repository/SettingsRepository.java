package de.rwth.idsg.steve.repository;

import de.rwth.idsg.steve.repository.dto.MailSettings;
import de.rwth.idsg.steve.web.dto.SettingsForm;

public interface SettingsRepository {
    SettingsForm getForm();
    MailSettings getMailSettings();
    int getHeartbeatIntervalInSeconds();
    int getHoursToExpire();
    void update(SettingsForm settingsForm);
}
