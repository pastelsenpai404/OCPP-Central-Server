package de.rwth.idsg.steve.utils;

import lombok.extern.slf4j.Slf4j;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.core.Appender;
import org.apache.logging.log4j.core.LoggerContext;
import org.apache.logging.log4j.core.appender.FileAppender;
import org.apache.logging.log4j.core.appender.MemoryMappedFileAppender;
import org.apache.logging.log4j.core.appender.RandomAccessFileAppender;
import org.apache.logging.log4j.core.appender.RollingFileAppender;
import org.apache.logging.log4j.core.appender.RollingRandomAccessFileAppender;
import org.apache.logging.log4j.core.config.Configuration;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.ThreadLocalRandom;

@Slf4j
public enum LogFileRetriever {
    INSTANCE;

    private final List<Path> logPathList;

    LogFileRetriever() {
        logPathList = getActiveLogFilePaths();
    }

    public Optional<Path> getPath() {
        Path p;
        if (logPathList.isEmpty()) {
            p = null;
        } else if (logPathList.size() == 1) {
            p = logPathList.get(0);
        } else {
            p = rollTheDice();
        }
        return Optional.ofNullable(p);
    }

    public String getLogFilePathOrErrorMessage() {
        return getPath().map(path -> path.toAbsolutePath().toString())
                        .orElseGet(this::getErrorMessage);
    }

    public String getErrorMessage() {
        return "Not available";
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private Path rollTheDice() {
        log.trace("Rolling the dice...");
        int index = ThreadLocalRandom.current().nextInt(logPathList.size());
        return logPathList.get(index);
    }

    private List<Path> getActiveLogFilePaths() {
        LoggerContext context = (LoggerContext) LogManager.getContext(false);
        Configuration config = context.getConfiguration();

        List<Path> fileNameList = new ArrayList<>();
        for (Appender appender : config.getAppenders().values()) {
            String fileName = extractFileName(appender);
            if (fileName != null) {
                fileNameList.add(Paths.get(fileName));
            }
        }

        return fileNameList;
    }

    private String extractFileName(Appender a) {
        if (a instanceof FileAppender) {
            return ((FileAppender) a).getFileName();

        } else if (a instanceof RollingFileAppender) {
            return ((RollingFileAppender) a).getFileName();

        } else if (a instanceof RollingRandomAccessFileAppender) {
            return ((RollingRandomAccessFileAppender) a).getFileName();

        } else if (a instanceof RandomAccessFileAppender) {
            return ((RandomAccessFileAppender) a).getFileName();

        } else if (a instanceof MemoryMappedFileAppender) {
            return ((MemoryMappedFileAppender) a).getFileName();

        } else {
            return null;
        }
    }
}
