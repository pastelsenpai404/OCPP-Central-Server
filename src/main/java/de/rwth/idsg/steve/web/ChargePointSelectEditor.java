package de.rwth.idsg.steve.web;

import de.rwth.idsg.steve.ocpp.OcppTransport;
import de.rwth.idsg.steve.repository.dto.ChargePointSelect;

import java.beans.PropertyEditorSupport;

public class ChargePointSelectEditor extends PropertyEditorSupport {

    @Override
    public void setAsText(String text) {
        if (!text.isEmpty()) {
            String[] chargePointItem = text.split(";");

            // chargePointItem[0] : ocpp transport type
            // chargePointItem[1] : chargebox id
            // chargePointItem[2] : endpoint (IP) address
            ChargePointSelect cps = new ChargePointSelect(
                    OcppTransport.fromName(chargePointItem[0]),
                    chargePointItem[1],
                    chargePointItem[2]
            );

            setValue(cps);
        }
    }
}
