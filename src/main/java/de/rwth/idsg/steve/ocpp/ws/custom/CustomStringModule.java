package de.rwth.idsg.steve.ocpp.ws.custom;

import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.core.Version;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.JavaType;
import com.fasterxml.jackson.databind.JsonMappingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.SerializerProvider;
import com.fasterxml.jackson.databind.deser.std.StringDeserializer;
import com.fasterxml.jackson.databind.jsonFormatVisitors.JsonFormatVisitorWrapper;
import com.fasterxml.jackson.databind.jsontype.TypeSerializer;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.databind.ser.std.StdScalarSerializer;
import org.owasp.encoder.Encode;

import java.io.IOException;
import java.lang.reflect.Type;

public class CustomStringModule extends SimpleModule {

    public CustomStringModule() {
        super("CustomStringModule", new Version(0, 0, 1, null, "de.rwth.idsg", "steve"));

        super.addSerializer(String.class, new CustomStringSerializer());
        super.addDeserializer(String.class, new CustomStringDeserializer());
    }

    /**
     * Since {@link com.fasterxml.jackson.databind.ser.std.StringSerializer} is marked as final, its contents are
     * copied here (and adjusted as needed).
     */
    private static class CustomStringSerializer extends StdScalarSerializer<Object> {

        private static final long serialVersionUID = 1L;

        public CustomStringSerializer() {
            super(String.class, false);
        }

        @Override
        public boolean isEmpty(SerializerProvider prov, Object value) {
            String str = (String) value;
            return str.isEmpty();
        }

        @Override
        public void serialize(Object value, JsonGenerator gen, SerializerProvider provider) throws IOException {
            gen.writeString(objectToString(value));
        }

        @Override
        public final void serializeWithType(Object value, JsonGenerator gen, SerializerProvider provider,
                                            TypeSerializer typeSer) throws IOException {
            gen.writeString(objectToString(value));
        }

        @Override
        public JsonNode getSchema(SerializerProvider provider, Type typeHint) {
            return createSchemaNode("string", true);
        }

        @Override
        public void acceptJsonFormatVisitor(JsonFormatVisitorWrapper visitor, JavaType typeHint) throws JsonMappingException {
            visitStringFormat(visitor, typeHint);
        }

        private static String objectToString(Object value) {
            return Encode.forHtml((String) value);
        }
    }

    private static class CustomStringDeserializer extends StringDeserializer {

        private static final long serialVersionUID = 1L;

        @Override
        public String deserialize(JsonParser p, DeserializationContext ctxt) throws IOException {
            String val = super.deserialize(p, ctxt);
            return Encode.forHtml(val);
        }
    }
}
