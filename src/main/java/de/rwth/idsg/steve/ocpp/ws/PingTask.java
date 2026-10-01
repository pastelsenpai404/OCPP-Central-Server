// package de.rwth.idsg.steve.ocpp.ws;

// import lombok.RequiredArgsConstructor;
// import lombok.extern.slf4j.Slf4j;
// import org.springframework.web.socket.PingMessage;
// import org.springframework.web.socket.WebSocketSession;

// import java.io.IOException;
// import java.nio.ByteBuffer;

// import static java.nio.charset.StandardCharsets.UTF_8;

// @Slf4j
// @RequiredArgsConstructor
// public class PingTask implements Runnable {
//     private final String chargeBoxId;
//     private final WebSocketSession session;

//     private static final PingMessage PING_MESSAGE = new PingMessage(ByteBuffer.wrap("ping".getBytes(UTF_8)));
//     private static final long PING_INTERVAL_MS = 10000; // 10 seconds interval between pings

//     @Override
//     public void run() {
//         while (session.isOpen()) {
//             WebSocketLogger.sendingPing(chargeBoxId, session);
//             try {
//                 session.sendMessage(PING_MESSAGE);
//             } catch (IOException e) {
//                 WebSocketLogger.pingError(chargeBoxId, session, e);
//                 break; // Exit the loop if there's an error sending the ping
//             }
//             try {
//                 Thread.sleep(PING_INTERVAL_MS); // Delay between pings
//             } catch (InterruptedException e) {
//                 Thread.currentThread().interrupt();
//                 break;
//             }
//         }
//     }
// }

// package de.rwth.idsg.steve.ocpp.ws;

// import lombok.RequiredArgsConstructor;
// import lombok.extern.slf4j.Slf4j;
// import org.springframework.web.socket.PingMessage;
// import org.springframework.web.socket.WebSocketSession;

// import java.io.IOException;
// import java.nio.ByteBuffer;
// import java.util.concurrent.Executors;
// import java.util.concurrent.ScheduledExecutorService;
// import java.util.concurrent.TimeUnit;

// import static java.nio.charset.StandardCharsets.UTF_8;

// @Slf4j
// @RequiredArgsConstructor
// public class PingTask implements Runnable {
//     private final String chargeBoxId;
//     private final WebSocketSession session;

//     private static final PingMessage PING_MESSAGE = new PingMessage(ByteBuffer.wrap("ping".getBytes(UTF_8)));
//     private static final long PING_INTERVAL_MS = 10000; // 10 seconds interval between pings

//     @Override
//     public void run() {
//         WebSocketLogger.sendingPing(chargeBoxId, session);
//         try {
//             session.sendMessage(PING_MESSAGE);
//         } catch (IOException e) {
//             WebSocketLogger.pingError(chargeBoxId, session, e);
//             // Schedule a retry or handle the error appropriately
//         }
//     }

//     public static void schedulePing(String chargeBoxId, WebSocketSession session) {
//         ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(1);
//         PingTask pingTask = new PingTask(chargeBoxId, session);
//         scheduler.scheduleAtFixedRate(pingTask, 0, PING_INTERVAL_MS, TimeUnit.MILLISECONDS);
//     }
// }


package de.rwth.idsg.steve.ocpp.ws;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.socket.PingMessage;
import org.springframework.web.socket.WebSocketSession;

import java.io.IOException;
import java.nio.ByteBuffer;

import static java.nio.charset.StandardCharsets.UTF_8;

@Slf4j
@RequiredArgsConstructor
public class PingTask implements Runnable {
    private final String chargeBoxId;
    private final WebSocketSession session;

    private static final PingMessage PING_MESSAGE = new PingMessage(ByteBuffer.wrap("ping".getBytes(UTF_8)));

    @Override
    public void run() {
        WebSocketLogger.sendingPing(chargeBoxId, session);
        try {
            session.sendMessage(PING_MESSAGE);
        } catch (IOException e) {
            WebSocketLogger.pingError(chargeBoxId, session, e);
            // TODO: Do something about this
        }
    }
}