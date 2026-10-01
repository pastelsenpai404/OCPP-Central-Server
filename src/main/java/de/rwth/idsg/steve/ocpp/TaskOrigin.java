package de.rwth.idsg.steve.ocpp;

import lombok.RequiredArgsConstructor;

@RequiredArgsConstructor
public enum TaskOrigin {

    // When the action was triggered by SteVe internally (e.g. by the admin/user)
    INTERNAL,

    // When the action was triggered by an external system (e.g. integrated roaming partner)
    EXTERNAL
}
