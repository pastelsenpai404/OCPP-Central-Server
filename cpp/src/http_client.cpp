#include "ocpp/http_client.hpp"
#include "ocpp/commands.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <memory>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
namespace ocpp {
std::string soap_origin(std::string_view url) {
    if (url.starts_with("https://"))
        return transfer_origin(url);
    if (!url.starts_with("http://"))
        throw ProtocolError("SecurityError", "Invalid SOAP URL scheme");
    // Unencrypted legacy SOAP is restricted to explicitly configured literal private IPv4.
    const auto secure = "https://" + std::string(url.substr(7));
    const auto origin = transfer_origin(secure);
    const auto authority = std::string_view(origin).substr(8);
    const auto host = authority.substr(0, authority.find(':'));
    std::array<unsigned, 4> octets{};
    std::size_t pos = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto end = host.find('.', pos);
        const auto part = host.substr(pos, end == host.npos ? end : end - pos);
        const auto r = std::from_chars(part.data(), part.data() + part.size(), octets[i]);
        if (part.empty() || (part.size() > 1 && part.front() == '0') || r.ec != std::errc{} ||
            r.ptr != part.data() + part.size() || octets[i] > 255 || (i < 3 && end == host.npos) ||
            (i == 3 && end != host.npos))
            throw ProtocolError("SecurityError",
                                "HTTP SOAP requires a literal private IPv4 address");
        pos = end + 1;
    }
    if (!(octets[0] == 127 || octets[0] == 10 ||
          (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
          (octets[0] == 192 && octets[1] == 168)))
        throw ProtocolError("SecurityError", "Public SOAP endpoint requires HTTPS");
    return "http://" + std::string(authority);
}
namespace {
[[noreturn]] void failed() {
    throw std::runtime_error("Outbound HTTP transport failed");
}
} // namespace
HttpResult post_http(const std::string &url, std::string_view content_type,
                     const std::string &authorization, const std::string &body) {
    if (body.size() > 65536 || content_type.find_first_of("\r\n") != content_type.npos ||
        authorization.find_first_of("\r\n") != authorization.npos)
        failed();
    static_cast<void>(soap_origin(url));
#ifdef _WIN32
    const std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts))
        failed();
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty())
        path = L"/";
    struct Handle {
        HINTERNET value;
        explicit Handle(HINTERNET v) : value(v) {
            if (!v)
                failed();
        }
        ~Handle() {
            WinHttpCloseHandle(value);
        }
        Handle(const Handle &) = delete;
        Handle &operator=(const Handle &) = delete;
    };
    Handle session(WinHttpOpen(L"OCPP-CSMS/1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!WinHttpSetTimeouts(session.value, 5000, 5000, 10000, 10000))
        failed();
    Handle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    Handle request(
        WinHttpOpenRequest(connection.value, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                           parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled,
                          sizeof(disabled)))
        failed();
    const auto headers = "Content-Type: " + std::string(content_type) +
                         "\r\nAuthorization: " + authorization + "\r\n";
    const std::wstring wh(headers.begin(), headers.end());
    if (!WinHttpSendRequest(request.value, wh.c_str(), static_cast<DWORD>(wh.size()),
                            const_cast<char *>(body.data()), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.value, nullptr))
        failed();
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &length,
                             WINHTTP_NO_HEADER_INDEX))
        failed();
    HttpResult result{static_cast<int>(status), {}};
    std::array<char, 4096> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (;;) {
        if (std::chrono::steady_clock::now() > deadline)
            failed();
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()),
                             &read))
            failed();
        if (!read)
            break;
        if (result.body.size() + read > 65536)
            failed();
        result.body.append(buffer.data(), read);
    }
    return result;
#else
    struct Global {
        Global() {
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                failed();
        }
        ~Global() {
            curl_global_cleanup();
        }
    };
    static const Global global;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle)
        failed();
    curl_slist *raw = nullptr;
    raw = curl_slist_append(raw, ("Content-Type: " + std::string(content_type)).c_str());
    raw = curl_slist_append(raw, ("Authorization: " + authorization).c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw, curl_slist_free_all);
    if (!headers)
        failed();
    HttpResult result{0, {}};
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_PROXY, "");
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(
        handle.get(), CURLOPT_WRITEFUNCTION,
        +[](char *data, std::size_t size, std::size_t count, void *context) -> std::size_t {
            auto &out = *static_cast<std::string *>(context);
            if (size && count > 65536 / size)
                return 0;
            const auto bytes = size * count;
            if (out.size() + bytes > 65536)
                return 0;
            out.append(data, bytes);
            return bytes;
        });
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &result.body);
    if (curl_easy_perform(handle.get()) != CURLE_OK)
        failed();
    long status = 0;
    if (curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK)
        failed();
    result.status = static_cast<int>(status);
    return result;
#endif
}
} // namespace ocpp
