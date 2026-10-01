package de.rwth.idsg.steve.repository;

import de.rwth.idsg.steve.ocpp.CommunicationTask;
import de.rwth.idsg.steve.repository.dto.TaskOverview;

import java.util.List;

public interface TaskStore {
    List<TaskOverview> getOverview();
    CommunicationTask get(Integer taskId);
    Integer add(CommunicationTask task);
    void clearFinished();
}
