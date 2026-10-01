package de.rwth.idsg.steve.utils;

import de.rwth.idsg.steve.SteveConfiguration;
import lombok.AccessLevel;
import lombok.NoArgsConstructor;

import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.URL;
import java.util.Arrays;
import java.util.List;

@NoArgsConstructor(access = AccessLevel.PRIVATE)
public final class InternetChecker {

    private static final int CONNECT_TIMEOUT = 5_000;
    private static final int READ_TIMEOUT = 5_000;

    private static final List<String> HOST_LIST = Arrays.asList(
            "https://treibhaus.informatik.rwth-aachen.de/heartbeat/",
            "https://github.com",
            "https://www.wikipedia.org",
            "https://www.google.com",
            "https://www.apple.com",
            "https://www.facebook.com"
    );

    static {
        System.setProperty("http.agent", "SteVe/" + SteveConfiguration.CONFIG.getSteveCompositeVersion());
    }

    public static boolean isInternetAvailable() {
        for (String s : HOST_LIST) {
            if (isHostAvailable(s)) {
                return true;
            }
        }

        return false;
    }

    private static boolean isHostAvailable(String str) {
        try {
            URL url = new URL(str);
            HttpURLConnection con = (HttpURLConnection) url.openConnection();
            con.setRequestProperty("Connection", "close");  // otherwise, default setting is "keep-alive"
            try {
                con.setConnectTimeout(CONNECT_TIMEOUT);
                con.setReadTimeout(READ_TIMEOUT);
                con.connect();
                if (con.getResponseCode() == HttpURLConnection.HTTP_OK) {
                    return true;
                }
            } finally {
                con.disconnect();
            }
        } catch (MalformedURLException e) {
            throw new RuntimeException(e);
        } catch (Exception e) {
            // No-op
        }

        return false;
    }
}
