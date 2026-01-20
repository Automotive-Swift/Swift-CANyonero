/**
 * TCP Transport Implementation
 */
#include "transport.h"

#ifdef _WIN32
// WIN32_LEAN_AND_MEAN defined via CMake
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
typedef int SOCKET;
#endif

#include <cstring>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace ecuconnect {

struct TcpTransport::Impl {
    TcpConfig config;
    SOCKET socket = INVALID_SOCKET;
    std::string lastError;
    bool wsaInitialized = false;

    Impl(const TcpConfig& cfg) : config(cfg) {}

    ~Impl() {
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
        }
#ifdef _WIN32
        if (wsaInitialized) {
            WSACleanup();
        }
#endif
    }

    bool initWsa() {
#ifdef _WIN32
        if (!wsaInitialized) {
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0) {
                std::ostringstream oss;
                oss << "WSAStartup failed: " << result;
                lastError = oss.str();
                return false;
            }
            wsaInitialized = true;
        }
#endif
        return true;
    }

    bool setSocketTimeout(uint32_t timeout_ms) {
#ifdef _WIN32
        DWORD timeout = timeout_ms;
        if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
            lastError = "Failed to set receive timeout";
            return false;
        }
#else
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            lastError = "Failed to set receive timeout";
            return false;
        }
#endif
        return true;
    }
};

TcpTransport::TcpTransport(const TcpConfig& config)
    : pImpl(std::make_unique<Impl>(config)) {
}

TcpTransport::~TcpTransport() = default;

bool TcpTransport::connect() {
    if (!pImpl->initWsa()) {
        return false;
    }

    // Create socket
    pImpl->socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (pImpl->socket == INVALID_SOCKET) {
        pImpl->lastError = "Failed to create socket";
        return false;
    }

    // Set up address
    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(pImpl->config.port);

    if (inet_pton(AF_INET, pImpl->config.host.c_str(), &serverAddr.sin_addr) <= 0) {
        pImpl->lastError = "Invalid address: " + pImpl->config.host;
        closesocket(pImpl->socket);
        pImpl->socket = INVALID_SOCKET;
        return false;
    }

    // Set non-blocking for connect timeout
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(pImpl->socket, FIONBIO, &mode);
#else
    int flags = fcntl(pImpl->socket, F_GETFL, 0);
    fcntl(pImpl->socket, F_SETFL, flags | O_NONBLOCK);
#endif

    // Connect
    int result = ::connect(pImpl->socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

#ifdef _WIN32
    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            // Wait for connection with timeout
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(pImpl->socket, &writeSet);

            struct timeval tv;
            tv.tv_sec = pImpl->config.connect_timeout_ms / 1000;
            tv.tv_usec = (pImpl->config.connect_timeout_ms % 1000) * 1000;

            result = select(0, nullptr, &writeSet, nullptr, &tv);
            if (result <= 0) {
                pImpl->lastError = "Connection timeout";
                closesocket(pImpl->socket);
                pImpl->socket = INVALID_SOCKET;
                return false;
            }

            // Check for connection error
            int optval;
            int optlen = sizeof(optval);
            if (getsockopt(pImpl->socket, SOL_SOCKET, SO_ERROR, (char*)&optval, &optlen) < 0 || optval != 0) {
                pImpl->lastError = "Connection failed";
                closesocket(pImpl->socket);
                pImpl->socket = INVALID_SOCKET;
                return false;
            }
        } else {
            std::ostringstream oss;
            oss << "Connect failed with error: " << err;
            pImpl->lastError = oss.str();
            closesocket(pImpl->socket);
            pImpl->socket = INVALID_SOCKET;
            return false;
        }
    }

    // Set back to blocking
    mode = 0;
    ioctlsocket(pImpl->socket, FIONBIO, &mode);
#else
    if (result < 0) {
        if (errno == EINPROGRESS) {
            struct pollfd pfd;
            pfd.fd = pImpl->socket;
            pfd.events = POLLOUT;

            result = poll(&pfd, 1, pImpl->config.connect_timeout_ms);
            if (result <= 0) {
                pImpl->lastError = "Connection timeout";
                closesocket(pImpl->socket);
                pImpl->socket = INVALID_SOCKET;
                return false;
            }

            int optval;
            socklen_t optlen = sizeof(optval);
            if (getsockopt(pImpl->socket, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0 || optval != 0) {
                pImpl->lastError = "Connection failed";
                closesocket(pImpl->socket);
                pImpl->socket = INVALID_SOCKET;
                return false;
            }
        } else {
            pImpl->lastError = "Connect failed";
            closesocket(pImpl->socket);
            pImpl->socket = INVALID_SOCKET;
            return false;
        }
    }

    // Set back to blocking
    flags = fcntl(pImpl->socket, F_GETFL, 0);
    fcntl(pImpl->socket, F_SETFL, flags & ~O_NONBLOCK);
#endif

    // Set receive timeout
    pImpl->setSocketTimeout(pImpl->config.receive_timeout_ms);

    // Disable Nagle algorithm for lower latency
    int flag = 1;
    setsockopt(pImpl->socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));

    return true;
}

void TcpTransport::disconnect() {
    if (pImpl->socket != INVALID_SOCKET) {
        closesocket(pImpl->socket);
        pImpl->socket = INVALID_SOCKET;
    }
}

bool TcpTransport::isConnected() const {
    return pImpl->socket != INVALID_SOCKET;
}

int TcpTransport::send(const std::vector<uint8_t>& data) {
    if (pImpl->socket == INVALID_SOCKET) {
        pImpl->lastError = "Not connected";
        return -1;
    }

    int result = ::send(pImpl->socket, reinterpret_cast<const char*>(data.data()),
                        static_cast<int>(data.size()), 0);

    if (result == SOCKET_ERROR) {
        pImpl->lastError = "Send failed";
        return -1;
    }

    return result;
}

std::vector<uint8_t> TcpTransport::receive(uint32_t timeout_ms) {
    if (pImpl->socket == INVALID_SOCKET) {
        pImpl->lastError = "Not connected";
        return {};
    }

    // Set timeout for this receive
    pImpl->setSocketTimeout(timeout_ms);

    // Use select for timeout
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(pImpl->socket, &readSet);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

#ifdef _WIN32
    int selectResult = select(0, &readSet, nullptr, nullptr, &tv);
#else
    int selectResult = select(pImpl->socket + 1, &readSet, nullptr, nullptr, &tv);
#endif

    if (selectResult <= 0) {
        // Timeout or error
        return {};
    }

    std::vector<uint8_t> buffer(4096);
    int received = ::recv(pImpl->socket, reinterpret_cast<char*>(buffer.data()),
                          static_cast<int>(buffer.size()), 0);

    if (received <= 0) {
        if (received == 0) {
            pImpl->lastError = "Connection closed by peer";
            disconnect();
        } else {
            pImpl->lastError = "Receive failed";
        }
        return {};
    }

    buffer.resize(received);
    return buffer;
}

std::string TcpTransport::getLastError() const {
    return pImpl->lastError;
}

namespace {

// Check if string looks like an IP address (contains dots and only digits/dots/colons)
bool looksLikeIpAddress(const std::string& s) {
    if (s.empty()) return false;

    // Must contain at least one dot
    if (s.find('.') == std::string::npos) return false;

    // All characters must be digits, dots, or colon (for port)
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != ':') {
            return false;
        }
    }
    return true;
}

// Case-insensitive prefix check
bool startsWithIgnoreCase(const std::string& str, const std::string& prefix) {
    if (str.length() < prefix.length()) return false;
    for (size_t i = 0; i < prefix.length(); i++) {
        if (std::tolower(static_cast<unsigned char>(str[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

// Transport factory with connection string parsing and auto-detection
std::unique_ptr<ITransport> createTransport(TransportType type, const std::string& connection_string) {
    std::string cs = connection_string;

    // Handle explicit BLE prefix: "BLE:..." or "ble:..."
    if (startsWithIgnoreCase(cs, "BLE:")) {
        BleConfig config;
        config.deviceNameOrAddress = cs.substr(4);
        return std::make_unique<BleTransport>(config);
    }

    // Handle explicit TCP prefix: "TCP:..." or "tcp:..."
    if (startsWithIgnoreCase(cs, "TCP:")) {
        cs = cs.substr(4);
        // Fall through to TCP parsing
    }

    // If TransportType::BLE_L2CAP was explicitly requested
    if (type == TransportType::BLE_L2CAP) {
        BleConfig config;
        config.deviceNameOrAddress = cs;
        return std::make_unique<BleTransport>(config);
    }

    // Auto-detect based on connection string format
    // - IP address (contains dots and digits) → TCP
    // - Otherwise (device name without dots) → BLE
    if (!cs.empty() && !looksLikeIpAddress(cs)) {
        // Looks like a BLE device name (e.g., "ECUconnect-XXXX")
        BleConfig config;
        config.deviceNameOrAddress = cs;
        return std::make_unique<BleTransport>(config);
    }

    // TCP transport (default or explicit)
    TcpConfig config;
    if (!cs.empty()) {
        // Parse host:port format
        size_t colonPos = cs.rfind(':');  // Use rfind in case of IPv6 future support
        if (colonPos != std::string::npos) {
            // Check if the part after colon is a valid port number
            std::string portStr = cs.substr(colonPos + 1);
            bool isPort = !portStr.empty() && std::all_of(portStr.begin(), portStr.end(),
                [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });

            if (isPort) {
                config.host = cs.substr(0, colonPos);
                config.port = static_cast<uint16_t>(std::stoi(portStr));
            } else {
                config.host = cs;
            }
        } else {
            config.host = cs;
        }
    }
    return std::make_unique<TcpTransport>(config);
}

} // namespace ecuconnect
