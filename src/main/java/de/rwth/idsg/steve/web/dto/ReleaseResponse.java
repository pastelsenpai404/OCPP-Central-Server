package de.rwth.idsg.steve.web.dto;

import lombok.Getter;
import lombok.Setter;
import lombok.ToString;

@Getter
@Setter
@ToString
public class ReleaseResponse {
    private String tagName;
    private String name;

    private String htmlUrl;
    private String tarballUrl;
    private String zipballUrl;
}
