package de.rwth.idsg.steve.ocpp.converter;

import java.util.function.Function;

public class Convert {

    public static <T, R> Function<T, R> start(T arg, Function<T, R> function) {
        return function;
    }

}
