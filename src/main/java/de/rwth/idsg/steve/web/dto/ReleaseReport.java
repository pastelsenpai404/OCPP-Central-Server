package de.rwth.idsg.steve.web.dto;

import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.Setter;
import lombok.ToString;

@Getter
@Setter
@ToString
@RequiredArgsConstructor
public class ReleaseReport {
    private final boolean moreRecent;

    private String githubVersion;

    private String htmlUrl;
    private String downloadUrl;
}
