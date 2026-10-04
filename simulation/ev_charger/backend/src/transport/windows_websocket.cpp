#define WIN32_LEAN_AND_MEAN
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <simulation/transport.hpp>
#include <thread>
#include <vector>
#include <windows.h>
#include <winhttp.h>

namespace simulation {
namespace {
std::wstring wide(const std::string &s) {
    return {s.begin(), s.end()};
}
class WindowsSocket final : public Transport {
    std::string url_, protocol_, authorization_;
    HINTERNET session_ = nullptr, connection_ = nullptr, socket_ = nullptr;
    std::atomic<bool> closed_{true};
    std::thread receiver_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> messages_;

  public:
    WindowsSocket(std::string url, std::string protocol, std::string authorization)
        : url_(std::move(url)), protocol_(std::move(protocol)),
          authorization_(std::move(authorization)) {}
    ~WindowsSocket() override {
        close();
    }
    bool connect() override {
        close();
        auto address = wide(url_);
        if (address.starts_with(L"wss:"))
            address.replace(0, 4, L"https:");
        else if (address.starts_with(L"ws:"))
            address.replace(0, 3, L"http:");
        else
            return false;
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
            static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) {
            std::cerr << "WebSocket URL parse failed " << GetLastError() << '\n';
            return false;
        }
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength)
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        session_ =
            WinHttpOpen(L"EV Simulation/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
        if (!session_)
            return false;
        WinHttpSetTimeouts(session_, 3000, 3000, 3000, 3000);
        DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls));
        connection_ = WinHttpConnect(session_, host.c_str(), parts.nPort, 0);
        if (!connection_)
            return false;
        auto request =
            WinHttpOpenRequest(connection_, L"GET", path.c_str(), nullptr, nullptr, nullptr,
                               parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (!request)
            return false;
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
        const auto headers = wide("Sec-WebSocket-Protocol: " + protocol_ +
                                  "\r\nAuthorization: " + authorization_ + "\r\n");
        bool success = WinHttpSendRequest(request, headers.c_str(),
                                          static_cast<DWORD>(headers.size()), nullptr, 0, 0, 0) &&
                       WinHttpReceiveResponse(request, nullptr);
        DWORD status = 0, size = sizeof(status);
        success =
            success &&
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                nullptr, &status, &size, nullptr) &&
            status == 101;
        wchar_t selected[64]{};
        size = sizeof(selected);
        success = success &&
                  WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Sec-WebSocket-Protocol",
                                      selected, &size, nullptr) &&
                  wide(protocol_) == selected;
        if (success)
            socket_ = WinHttpWebSocketCompleteUpgrade(request, 0);
        WinHttpCloseHandle(request);
        if (!socket_) {
            std::cerr << "WebSocket handshake failed, HTTP " << status << ", Windows error "
                      << GetLastError() << '\n';
            return false;
        }
        DWORD timeout = 1000;
        WinHttpSetOption(socket_, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &timeout,
                         sizeof(timeout));
        closed_ = false;
        receiver_ = std::thread([this] {
            std::string message;
            std::vector<char> buffer(4096);
            while (!closed_) {
                DWORD count = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE kind{};
                if (WinHttpWebSocketReceive(socket_, buffer.data(),
                                            static_cast<DWORD>(buffer.size()), &count,
                                            &kind) != NO_ERROR)
                    break;
                if (kind != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE &&
                    kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                    break;
                if (message.size() + count > 65536)
                    break;
                message.append(buffer.data(), count);
                if (kind == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                    std::lock_guard lock(mutex_);
                    if (messages_.size() >= 32)
                        break;
                    messages_.push_back(std::move(message));
                    message.clear();
                    ready_.notify_one();
                }
            }
            closed_ = true;
            ready_.notify_one();
        });
        return true;
    }
    bool send(const std::string &data) override {
        return !closed_ && data.size() <= 65536 &&
               WinHttpWebSocketSend(socket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                    const_cast<char *>(data.data()),
                                    static_cast<DWORD>(data.size())) == NO_ERROR;
    }
    Read read(std::string &data) override {
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock, std::chrono::milliseconds(100),
                        [this] { return closed_ || !messages_.empty(); });
        if (!messages_.empty()) {
            data = std::move(messages_.front());
            messages_.pop_front();
            return Read::Text;
        }
        return closed_ ? Read::Closed : Read::Timeout;
    }
    void close() override {
        closed_ = true;
        if (socket_)
            WinHttpWebSocketClose(socket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        if (receiver_.joinable())
            receiver_.join();
        if (socket_)
            WinHttpCloseHandle(socket_);
        if (connection_)
            WinHttpCloseHandle(connection_);
        if (session_)
            WinHttpCloseHandle(session_);
        socket_ = connection_ = session_ = nullptr;
        std::lock_guard lock(mutex_);
        messages_.clear();
    }
};
} // namespace
std::unique_ptr<Transport> websocket(const std::string &url, const std::string &protocol,
                                     const std::string &authorization, const std::string &) {
    return std::make_unique<WindowsSocket>(url, protocol, authorization);
}
} // namespace simulation
