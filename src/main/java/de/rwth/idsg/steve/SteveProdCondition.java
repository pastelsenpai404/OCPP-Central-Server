package de.rwth.idsg.steve;

import org.springframework.context.annotation.Condition;
import org.springframework.context.annotation.ConditionContext;
import org.springframework.context.annotation.Conditional;
import org.springframework.context.annotation.Profile;
import org.springframework.core.type.AnnotatedTypeMetadata;

import static de.rwth.idsg.steve.SteveConfiguration.CONFIG;


public class SteveProdCondition implements Condition {

    @Override
    public boolean matches(ConditionContext context, AnnotatedTypeMetadata metadata) {
        return CONFIG.getProfile().isProd();
    }
}
