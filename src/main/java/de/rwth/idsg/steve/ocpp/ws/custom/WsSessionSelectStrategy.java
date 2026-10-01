package de.rwth.idsg.steve.ocpp.ws.custom;

import de.rwth.idsg.steve.ocpp.ws.data.SessionContext;
import org.springframework.web.socket.WebSocketSession;

import java.util.Deque;

public interface WsSessionSelectStrategy {
    WebSocketSession getSession(Deque<SessionContext> sessionContexts);
}
