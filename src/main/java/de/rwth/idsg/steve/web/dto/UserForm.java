package de.rwth.idsg.steve.web.dto;

import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import org.joda.time.LocalDate;

import javax.validation.constraints.Email;
import javax.validation.constraints.NotNull;

@Getter
@Setter
@ToString
public class UserForm {

    // Internal database id
    private Integer userPk;

    private String ocppIdTag;

    private String firstName;
    private String lastName;
    private LocalDate birthDay;
    private String phone;
    private String note;

    @NotNull(message = "Sex is required")
    private UserSex sex;

    @Email(message = "Not a valid e-mail address")
    private String eMail;

    private Address address;

}
