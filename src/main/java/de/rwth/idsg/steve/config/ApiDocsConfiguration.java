package de.rwth.idsg.steve.config;

import de.rwth.idsg.steve.SteveConfiguration;
import de.rwth.idsg.steve.SteveProdCondition;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Conditional;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.bind.annotation.RestController;
import springfox.documentation.builders.ApiInfoBuilder;
import springfox.documentation.oas.annotations.EnableOpenApi;
import springfox.documentation.spi.DocumentationType;
import springfox.documentation.spring.web.plugins.Docket;

import static springfox.documentation.builders.RequestHandlerSelectors.withClassAnnotation;

@Configuration
@EnableOpenApi
@Conditional(SteveProdCondition.class)
public class ApiDocsConfiguration {

    static {
        System.setProperty("springfox.documentation.open-api.v3.path", "/manager/v3/api-docs");
    }

    @Bean
    public Docket apiDocs() {
        String title = "REST API Documentation";

        var apiInfo = new ApiInfoBuilder()
            .title(title)
            .description(title)
            .license("GPL-3.0")
            .licenseUrl("") //GNU
            .version(SteveConfiguration.CONFIG.getSteveVersion())
            .build();

        return new Docket(DocumentationType.OAS_30)
            .useDefaultResponseMessages(false)
            .apiInfo(apiInfo)
            .select()
            .apis(withClassAnnotation(RestController.class))
            .build();
    }
}
