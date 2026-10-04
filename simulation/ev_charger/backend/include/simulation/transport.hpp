#pragma once
#include <memory>
#include <string>
namespace simulation {
enum class Read { Text, Timeout, Closed };
class Transport {
  public:
    virtual ~Transport() = default;
    virtual bool connect() = 0;
    virtual bool send(const std::string &) = 0;
    virtual Read read(std::string &) = 0;
    virtual void close() = 0;
};
std::unique_ptr<Transport> websocket(const std::string &url, const std::string &protocol,
                                     const std::string &authorization, const std::string &ca_file);
} // namespace simulation
