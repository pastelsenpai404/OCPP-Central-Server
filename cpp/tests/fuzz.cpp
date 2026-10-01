#include "ocpp/protocol.hpp"
extern "C" int LLVMFuzzerTestOneInput(const unsigned char *data, std::size_t size) {
    try {
        static_cast<void>(
            ocpp::parse(std::string_view(reinterpret_cast<const char *>(data), size)));
    } catch (const std::exception &) {
    }
    return 0;
}
