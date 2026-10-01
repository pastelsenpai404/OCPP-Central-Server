#include "ocpp/protocol.hpp"
#include <chrono>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    ocpp::Schemas schemas(argv[1]);
    constexpr int iterations = 100000;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        const auto m = ocpp::parse("[2,\"benchmark\",\"Heartbeat\",{}]");
        schemas.validate(m.action, m.payload);
    }
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Heartbeat parse+schema: " << iterations / elapsed << " messages/s, "
              << elapsed * 1e6 / iterations << " us/message\n";
    std::cout << "Microbenchmark only; excludes network, TLS, SQL and billing.\n";
}
