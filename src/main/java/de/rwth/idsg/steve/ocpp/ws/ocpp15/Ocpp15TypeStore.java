package de.rwth.idsg.steve.ocpp.ws.ocpp15;

import de.rwth.idsg.steve.ocpp.ws.AbstractTypeStore;

public final class Ocpp15TypeStore extends AbstractTypeStore {

    public static final Ocpp15TypeStore INSTANCE = new Ocpp15TypeStore();

    private Ocpp15TypeStore() {
        super(
                ocpp.cs._2012._06.ObjectFactory.class.getPackage().getName(),
                ocpp.cp._2012._06.ObjectFactory.class.getPackage().getName()
        );
    }

}
