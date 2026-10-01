package de.rwth.idsg.steve.ocpp.soap;

import lombok.extern.slf4j.Slf4j;
import org.apache.cxf.interceptor.Fault;
import org.apache.cxf.message.Message;
import org.apache.cxf.phase.AbstractPhaseInterceptor;
import org.apache.cxf.phase.Phase;
import org.apache.cxf.ws.addressing.AddressingProperties;
import org.apache.cxf.ws.addressing.ContextUtils;

import static org.apache.cxf.ws.addressing.JAXWSAConstants.ADDRESSING_PROPERTIES_INBOUND;

@Slf4j
public class MessageIdInterceptor extends AbstractPhaseInterceptor<Message> {

    public MessageIdInterceptor() {
        super(Phase.PRE_LOGICAL);
        addBefore(org.apache.cxf.ws.addressing.impl.MAPAggregatorImpl.class.getName());
    }

    @Override
    public void handleMessage(Message message) throws Fault {
        AddressingProperties addressProp = (AddressingProperties) message.get(ADDRESSING_PROPERTIES_INBOUND);

        // Ws-Addressing is not used in the message. Early exit
        if (addressProp == null) {
            return;
        }

        if (addressProp.getMessageID() == null) {
            log.debug("The required MessageID element is missing! Adding one to the incoming message");
            addressProp.setMessageID(ContextUtils.getAttributedURI(ContextUtils.generateUUID()));
        }
    }

}
