#include "RendezvousClient.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <stdexcept>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), len);
    return result;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), len, nullptr, nullptr);
    return result;
}

// Pulls the value out of a flat {"key":"value"} JSON object. Only meant for
// this project's own tiny, fixed-shape rendezvous server responses - not a
// general JSON parser.
std::optional<std::string> ExtractJsonStringField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t start = json.find(needle);
    if (start == std::string::npos) return std::nullopt;
    start += needle.size();
    size_t end = json.find('"', start);
    if (end == std::string::npos) return std::nullopt;
    return json.substr(start, end - start);
}

void ThrowLastError(const std::string& what) {
    throw std::runtime_error(what + " failed (WinHTTP error " + std::to_string(GetLastError()) + ")");
}

}  // namespace

RendezvousClient::RendezvousClient(std::string host, uint16_t port, bool useTls)
    : host_(std::move(host)), port_(port), useTls_(useTls), sessionHandle_(nullptr), webSocketHandle_(nullptr) {
    HINTERNET session = WinHttpOpen(L"PhoneCursorServer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) ThrowLastError("WinHttpOpen");
    sessionHandle_ = session;
}

RendezvousClient::~RendezvousClient() {
    Close();
    if (sessionHandle_) WinHttpCloseHandle(static_cast<HINTERNET>(sessionHandle_));
}

void RendezvousClient::Close() {
    if (webSocketHandle_) {
        WinHttpWebSocketClose(static_cast<HINTERNET>(webSocketHandle_), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                               nullptr, 0);
        WinHttpCloseHandle(static_cast<HINTERNET>(webSocketHandle_));
        webSocketHandle_ = nullptr;
    }
}

std::string RendezvousClient::RequestPairingCode() {
    HINTERNET connect = WinHttpConnect(static_cast<HINTERNET>(sessionHandle_), Widen(host_).c_str(), port_, 0);
    if (!connect) ThrowLastError("WinHttpConnect");

    HINTERNET request = WinHttpOpenRequest(connect, L"POST", L"/session", nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, useTls_ ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        ThrowLastError("WinHttpOpenRequest");
    }

    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);

    std::string body;
    if (ok) {
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read)) break;
            body.append(buffer.data(), read);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);

    if (!ok) ThrowLastError("RequestPairingCode");
    auto code = ExtractJsonStringField(body, "code");
    if (!code) throw std::runtime_error("Rendezvous server returned an unexpected response: " + body);
    return *code;
}

void RendezvousClient::Connect(const std::string& pairingCode, const std::string& role) {
    HINTERNET connect = WinHttpConnect(static_cast<HINTERNET>(sessionHandle_), Widen(host_).c_str(), port_, 0);
    if (!connect) ThrowLastError("WinHttpConnect");

    std::wstring path = L"/session/" + Widen(pairingCode) + L"/ws?role=" + Widen(role);
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, useTls_ ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        ThrowLastError("WinHttpOpenRequest");
    }

    WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);

    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                         &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    HINTERNET webSocket = ok && statusCode == 101 ? WinHttpWebSocketCompleteUpgrade(request, 0) : nullptr;
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);

    if (!webSocket) {
        if (statusCode == 404) throw std::runtime_error("Pairing code not found or expired");
        if (statusCode == 409) throw std::runtime_error("A " + role + " is already connected with that pairing code");
        ThrowLastError("Connect (WebSocket upgrade)");
    }
    webSocketHandle_ = webSocket;
}

void RendezvousClient::SendCandidate(const std::string& ipPort) {
    if (!webSocketHandle_) throw std::runtime_error("SendCandidate called before Connect");
    DWORD result = WinHttpWebSocketSend(static_cast<HINTERNET>(webSocketHandle_),
                                         WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                         const_cast<char*>(ipPort.data()), static_cast<DWORD>(ipPort.size()));
    if (result != NO_ERROR) throw std::runtime_error("WinHttpWebSocketSend failed (error " + std::to_string(result) + ")");
}

std::optional<std::string> RendezvousClient::ReceiveCandidate(int timeoutMs) {
    if (!webSocketHandle_) throw std::runtime_error("ReceiveCandidate called before Connect");
    HINTERNET ws = static_cast<HINTERNET>(webSocketHandle_);

    DWORD timeout = static_cast<DWORD>(timeoutMs);
    WinHttpSetTimeouts(ws, static_cast<int>(timeout), static_cast<int>(timeout), static_cast<int>(timeout),
                        static_cast<int>(timeout));

    std::vector<char> buffer(1024);
    DWORD bytesRead = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType;
    DWORD result = WinHttpWebSocketReceive(ws, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, &bufferType);
    if (result != NO_ERROR) return std::nullopt;
    if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return std::nullopt;

    return std::string(buffer.data(), bytesRead);
}
