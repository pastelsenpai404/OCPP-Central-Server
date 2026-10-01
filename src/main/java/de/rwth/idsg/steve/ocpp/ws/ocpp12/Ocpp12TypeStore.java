package de.rwth.idsg.steve.ocpp.ws.ocpp12;

import de.rwth.idsg.steve.ocpp.ws.AbstractTypeStore;

public final class Ocpp12TypeStore extends AbstractTypeStore {

    public static final Ocpp12TypeStore INSTANCE = new Ocpp12TypeStore();

    private Ocpp12TypeStore() {
        super(
                ocpp.cs._2010._08.ObjectFactory.class.getPackage().getName(),
                ocpp.cp._2010._08.ObjectFactory.class.getPackage().getName()
        );
    }
}
