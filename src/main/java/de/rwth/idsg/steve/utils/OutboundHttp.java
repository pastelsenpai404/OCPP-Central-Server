package de.rwth.idsg.steve.utils;

import lombok.extern.slf4j.Slf4j;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

@Slf4j
public final class OutboundHttp {

    private static final int CONNECT_TIMEOUT_MS = 5_000;
    private static final int READ_TIMEOUT_MS = 10_000;
    private static final int QUEUE_CAPACITY = 200;

    private static final ThreadPoolExecutor EXECUTOR = new ThreadPoolExecutor(
            2,
            8,
            60L,
            TimeUnit.SECONDS,
            new ArrayBlockingQueue<>(QUEUE_CAPACITY),
            daemonThreadFactory(),
            (r, executor) -> log.warn("Outbound HTTP queue is full. Dropping task: {}", r));

    static {
        EXECUTOR.allowCoreThreadTimeOut(true);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> EXECUTOR.shutdownNow(), "outbound-http-shutdown"));
    }

    private OutboundHttp() {
    }

    public static void submit(String name, CheckedRunnable task) {
        EXECUTOR.execute(new NamedTask(name, task));
    }

    public static void postJson(String endpoint, String jsonInputString) throws Exception {
        HttpURLConnection connection = open(endpoint, "POST");
        connection.setRequestProperty("Cache-Control", "no-cache");
        connection.setRequestProperty("Content-Type", "application/json");
        connection.setDoOutput(true);

        try {
            try (OutputStream os = connection.getOutputStream()) {
                byte[] input = jsonInputString.getBytes(StandardCharsets.UTF_8);
                os.write(input, 0, input.length);
            }

            int responseCode = connection.getResponseCode();
            log.info("HTTP Response Code: {}", responseCode);
            log.info("Server Response: {}", readResponse(connection, responseCode));
        } finally {
            connection.disconnect();
        }
    }

    public static void get(String endpoint) throws Exception {
        HttpURLConnection connection = open(endpoint, "GET");

        try {
            int responseCode = connection.getResponseCode();
            log.info("HTTP Response Code: {}", responseCode);
            log.info("Server Response: {}", readResponse(connection, responseCode));
        } finally {
            connection.disconnect();
        }
    }

    private static HttpURLConnection open(String endpoint, String method) throws Exception {
        URL url = new URL(endpoint);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setRequestMethod(method);
        connection.setRequestProperty("Accept", "*/*");
        connection.setRequestProperty("Connection", "close");
        return connection;
    }

    private static String readResponse(HttpURLConnection connection, int responseCode) throws Exception {
        InputStream inputStream = responseCode >= 400 ? connection.getErrorStream() : connection.getInputStream();
        if (inputStream == null) {
            return "";
        }

        try (BufferedReader br = new BufferedReader(new InputStreamReader(inputStream, StandardCharsets.UTF_8))) {
            StringBuilder response = new StringBuilder();
            String responseLine;
            while ((responseLine = br.readLine()) != null) {
                response.append(responseLine.trim());
            }
            return response.toString();
        }
    }

    private static ThreadFactory daemonThreadFactory() {
        AtomicInteger counter = new AtomicInteger();
        return runnable -> {
            Thread thread = new Thread(runnable, "outbound-http-" + counter.incrementAndGet());
            thread.setDaemon(true);
            return thread;
        };
    }

    @FunctionalInterface
    public interface CheckedRunnable {
        void run() throws Exception;
    }

    private static final class NamedTask implements Runnable {
        private final String name;
        private final CheckedRunnable delegate;

        private NamedTask(String name, CheckedRunnable delegate) {
            this.name = name;
            this.delegate = delegate;
        }

        @Override
        public void run() {
            try {
                delegate.run();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                log.warn("Outbound HTTP task interrupted: {}", name, e);
            } catch (Exception e) {
                log.error("Outbound HTTP task failed: {}", name, e);
            }
        }

        @Override
        public String toString() {
            return name;
        }
    }
}
