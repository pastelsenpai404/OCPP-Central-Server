#include "simulation/transport.hpp"
#include <chrono>
#include <httplib.h>
namespace simulation {
class Socket final : public Transport {
    httplib::ws::WebSocketClient client;
    std::string protocol;

  public:
    Socket(const std::string &url, const std::string &p, const std::string &auth,
           const std::string &ca)
        : client(url, {{"Authorization", auth}, {"Sec-WebSocket-Protocol", p}}), protocol(p) {
        client.set_connection_timeout(3, 0);
        client.set_read_timeout(std::chrono::milliseconds(100));
        client.set_write_timeout(3, 0);
        if (!ca.empty())
            client.set_ca_cert_path(ca);
        client.enable_server_certificate_verification(true);
        client.enable_server_hostname_verification(true);
        client.set_websocket_ping_interval(30);
    }
    bool connect() override {
        return static_cast<bool>(client.connect()) && client.subprotocol() == protocol;
    }
    bool send(const std::string &wire) override {
        return client.send(wire);
    }
    Read read(std::string &wire) override {
        const auto result = client.read(wire);
        if (wire.size() > 65536) {
            close();
            return Read::Closed;
        }
        return result == httplib::ws::Text      ? Read::Text
               : result == httplib::ws::Timeout ? Read::Timeout
                                                : Read::Closed;
    }
    void close() override {
        client.close();
    }
};
std::unique_ptr<Transport> websocket(const std::string &url, const std::string &protocol,
                                     const std::string &auth, const std::string &ca) {
    return std::make_unique<Socket>(url, protocol, auth, ca);
}
} // namespace simulation
