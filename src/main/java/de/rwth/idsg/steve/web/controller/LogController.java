package de.rwth.idsg.steve.web.controller;

import de.rwth.idsg.steve.utils.LogFileRetriever;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestMethod;

import javax.servlet.http.HttpServletResponse;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Collections; 
import java.util.List;
import java.util.Optional;

@Slf4j
@Controller
@RequestMapping(value = "/manager")
public class LogController {

    @RequestMapping(value = "/log", method = RequestMethod.GET)
    public void log(HttpServletResponse response) {
        response.setContentType("text/plain");

        // try (PrintWriter writer = response.getWriter()) {
        // Optional<Path> p = LogFileRetriever.INSTANCE.getPath();
        // if (p.isPresent()) {
        // Files.lines(p.get(), StandardCharsets.UTF_8)
        // .forEach(writer::println);
        // } else {
        // writer.write(LogFileRetriever.INSTANCE.getErrorMessage());
        // }
        // } catch (IOException e) {
        // log.error("Exception happened", e);
        // }

        try (PrintWriter writer = response.getWriter()) {
            Optional<Path> p = LogFileRetriever.INSTANCE.getPath();
            if (p.isPresent()) {
                // Read all lines into a list
                List<String> lines = Files.readAllLines(p.get(), StandardCharsets.UTF_8);

                // Reverse the list
                Collections.reverse(lines);

                // Write each line to the PrintWriter
                lines.forEach(writer::println);
            } else {
                writer.write(LogFileRetriever.INSTANCE.getErrorMessage());
            }
        } catch (IOException e) {
            log.error("Exception happened", e);
        }
    }

    public String getLogFilePath() {
        return LogFileRetriever.INSTANCE.getLogFilePathOrErrorMessage();
    }

}
