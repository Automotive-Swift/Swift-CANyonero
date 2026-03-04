#include <Protocol.hpp>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kDefaultEndpoint = "192.168.42.42:129";
constexpr size_t kMaxPduSize = 0x10003;
constexpr int kPollIntervalMs = 100;

volatile sig_atomic_t gRunning = 1;

void onSignal(int) {
    gRunning = 0;
}

struct Options {
    std::string endpoint = kDefaultEndpoint;
    std::string canIf = "vcan0";
    uint32_t bitrate = 500000;
    uint32_t dataBitrate = 2000000;
    bool fd = false;
    int reconnectMs = 1000;
    int connectTimeoutMs = 3000;
    int commandTimeoutMs = 2000;
    int statsIntervalSec = 5;
    uint16_t rxStminUs = 0;
    uint16_t txStminUs = 0;
    std::optional<uint32_t> fixedRequestId;
    uint32_t replyPattern = 0;
    uint32_t replyMask = 0;
    bool recvOwn = false;
    bool verbose = false;
};

struct Stats {
    uint64_t canRx = 0;
    uint64_t canTx = 0;
    uint64_t ecuRx = 0;
    uint64_t ecuTx = 0;
    uint64_t droppedNoLink = 0;
    uint64_t droppedTooLarge = 0;
    uint64_t protocolErrors = 0;
    uint64_t ecuErrors = 0;
};

std::string hexId(uint32_t id) {
    std::ostringstream oss;
    if (id <= 0x7FF) {
        oss << "0x" << std::uppercase << std::hex << std::setw(3) << std::setfill('0') << id;
    } else {
        oss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << id;
    }
    return oss.str();
}

void usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "\n"
        << "Bridge SocketCAN <-> ECUconnect CANyonero ProtocolMachine over TCP.\n"
        << "\n"
        << "Options:\n"
        << "  --endpoint HOST:PORT      ECUconnect TCP endpoint (default: " << kDefaultEndpoint << ")\n"
        << "  --can-if IFACE            SocketCAN interface (default: vcan0)\n"
        << "  --bitrate BPS             CAN nominal bitrate for ECU channel open (default: 500000)\n"
        << "  --data-bitrate BPS        CAN FD data bitrate (default: 2000000)\n"
        << "  --fd                      Use raw_fd channel and allow CAN FD payloads\n"
        << "  --reconnect-ms MS         Reconnect interval after disconnect (default: 1000)\n"
        << "  --connect-timeout-ms MS   TCP connect timeout (default: 3000)\n"
        << "  --command-timeout-ms MS   Handshake command timeout (default: 2000)\n"
        << "  --rx-stmin-us US          RX separation time for channel open (default: 0)\n"
        << "  --tx-stmin-us US          TX separation time for channel open (default: 0)\n"
        << "  --request-id ID           Fixed TX CAN ID (hex or decimal). If unset, dynamic per frame\n"
        << "  --reply-pattern ID        RX filter pattern (default: 0x0)\n"
        << "  --reply-mask MASK         RX filter mask (default: 0x0 = accept all)\n"
        << "  --recv-own                Receive own CAN frames on this socket\n"
        << "  --stats-interval SEC      Periodic stats print interval, 0 disables (default: 5)\n"
        << "  --verbose                 Verbose protocol logging\n"
        << "  --help                    Show this help\n";
}

bool parseU32(const std::string& text, uint32_t& out) {
    try {
        size_t idx = 0;
        unsigned long value = std::stoul(text, &idx, 0);
        if (idx != text.size() || value > 0xFFFFFFFFUL) {
            return false;
        }
        out = static_cast<uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseI32(const std::string& text, int& out) {
    try {
        size_t idx = 0;
        long value = std::stol(text, &idx, 0);
        if (idx != text.size() || value < INT32_MIN || value > INT32_MAX) {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseU16(const std::string& text, uint16_t& out) {
    uint32_t tmp = 0;
    if (!parseU32(text, tmp) || tmp > 0xFFFF) {
        return false;
    }
    out = static_cast<uint16_t>(tmp);
    return true;
}

bool parseEndpoint(const std::string& endpoint, std::string& host, uint16_t& port) {
    auto pos = endpoint.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos == endpoint.size() - 1) {
        return false;
    }
    host = endpoint.substr(0, pos);
    uint16_t parsedPort = 0;
    if (!parseU16(endpoint.substr(pos + 1), parsedPort) || parsedPort == 0) {
        return false;
    }
    port = parsedPort;
    return true;
}

Options parseArgs(int argc, char** argv) {
    Options opts;

    static option longOpts[] = {
        {"endpoint", required_argument, nullptr, 'e'},
        {"can-if", required_argument, nullptr, 'i'},
        {"bitrate", required_argument, nullptr, 'b'},
        {"data-bitrate", required_argument, nullptr, 'B'},
        {"fd", no_argument, nullptr, 'f'},
        {"reconnect-ms", required_argument, nullptr, 'r'},
        {"connect-timeout-ms", required_argument, nullptr, 't'},
        {"command-timeout-ms", required_argument, nullptr, 'T'},
        {"rx-stmin-us", required_argument, nullptr, 1001},
        {"tx-stmin-us", required_argument, nullptr, 1002},
        {"request-id", required_argument, nullptr, 1003},
        {"reply-pattern", required_argument, nullptr, 1004},
        {"reply-mask", required_argument, nullptr, 1005},
        {"recv-own", no_argument, nullptr, 1006},
        {"stats-interval", required_argument, nullptr, 's'},
        {"verbose", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    while (true) {
        int idx = 0;
        int c = getopt_long(argc, argv, "e:i:b:B:r:t:T:s:vfh", longOpts, &idx);
        if (c == -1) {
            break;
        }
        switch (c) {
            case 'e':
                opts.endpoint = optarg;
                break;
            case 'i':
                opts.canIf = optarg;
                break;
            case 'b': {
                uint32_t value = 0;
                if (!parseU32(optarg, value)) {
                    throw std::runtime_error("Invalid --bitrate");
                }
                opts.bitrate = value;
                break;
            }
            case 'B': {
                uint32_t value = 0;
                if (!parseU32(optarg, value)) {
                    throw std::runtime_error("Invalid --data-bitrate");
                }
                opts.dataBitrate = value;
                break;
            }
            case 'f':
                opts.fd = true;
                break;
            case 'r': {
                int value = 0;
                if (!parseI32(optarg, value) || value < 100) {
                    throw std::runtime_error("Invalid --reconnect-ms (>=100)");
                }
                opts.reconnectMs = value;
                break;
            }
            case 't': {
                int value = 0;
                if (!parseI32(optarg, value) || value < 100) {
                    throw std::runtime_error("Invalid --connect-timeout-ms (>=100)");
                }
                opts.connectTimeoutMs = value;
                break;
            }
            case 'T': {
                int value = 0;
                if (!parseI32(optarg, value) || value < 100) {
                    throw std::runtime_error("Invalid --command-timeout-ms (>=100)");
                }
                opts.commandTimeoutMs = value;
                break;
            }
            case 's': {
                int value = 0;
                if (!parseI32(optarg, value) || value < 0) {
                    throw std::runtime_error("Invalid --stats-interval (>=0)");
                }
                opts.statsIntervalSec = value;
                break;
            }
            case 'v':
                opts.verbose = true;
                break;
            case 1001: {
                uint16_t value = 0;
                if (!parseU16(optarg, value)) {
                    throw std::runtime_error("Invalid --rx-stmin-us");
                }
                opts.rxStminUs = value;
                break;
            }
            case 1002: {
                uint16_t value = 0;
                if (!parseU16(optarg, value)) {
                    throw std::runtime_error("Invalid --tx-stmin-us");
                }
                opts.txStminUs = value;
                break;
            }
            case 1003: {
                uint32_t value = 0;
                if (!parseU32(optarg, value) || value > 0x1FFFFFFF) {
                    throw std::runtime_error("Invalid --request-id (0..0x1FFFFFFF)");
                }
                opts.fixedRequestId = value;
                break;
            }
            case 1004: {
                uint32_t value = 0;
                if (!parseU32(optarg, value) || value > 0x1FFFFFFF) {
                    throw std::runtime_error("Invalid --reply-pattern (0..0x1FFFFFFF)");
                }
                opts.replyPattern = value;
                break;
            }
            case 1005: {
                uint32_t value = 0;
                if (!parseU32(optarg, value)) {
                    throw std::runtime_error("Invalid --reply-mask");
                }
                opts.replyMask = value;
                break;
            }
            case 1006:
                opts.recvOwn = true;
                break;
            case 'h':
                usage(argv[0]);
                std::exit(0);
            default:
                usage(argv[0]);
                throw std::runtime_error("Invalid arguments");
        }
    }

    std::string host;
    uint16_t port = 0;
    if (!parseEndpoint(opts.endpoint, host, port)) {
        throw std::runtime_error("Invalid --endpoint, expected HOST:PORT");
    }

    return opts;
}

class PDUStream {
public:
    std::vector<CANyonero::PDU> feed(const uint8_t* data, size_t len) {
        buffer_.insert(buffer_.end(), data, data + len);

        std::vector<CANyonero::PDU> out;
        while (!buffer_.empty()) {
            if (CANyonero::PDU::exceedsMaxPduSize(buffer_, kMaxPduSize)) {
                throw std::runtime_error("Incoming PDU exceeds maximum size");
            }

            int scan = CANyonero::PDU::scanBuffer(buffer_);
            if (scan > 0) {
                std::vector<uint8_t> frame(buffer_.begin(), buffer_.begin() + scan);
                buffer_.erase(buffer_.begin(), buffer_.begin() + scan);
                out.emplace_back(frame);
            } else if (scan < 0) {
                size_t drop = static_cast<size_t>(-scan);
                drop = std::min(drop, buffer_.size());
                buffer_.erase(buffer_.begin(), buffer_.begin() + drop);
            } else {
                break;
            }
        }
        return out;
    }

private:
    std::vector<uint8_t> buffer_;
};

int openSocketCan(const std::string& ifname, bool enableFd, bool recvOwn) {
    int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        throw std::runtime_error("socket(PF_CAN) failed: " + std::string(std::strerror(errno)));
    }

    int recvOwnFlag = recvOwn ? 1 : 0;
    if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recvOwnFlag, sizeof(recvOwnFlag)) < 0) {
        ::close(fd);
        throw std::runtime_error("setsockopt(CAN_RAW_RECV_OWN_MSGS) failed: " + std::string(std::strerror(errno)));
    }

    if (enableFd) {
        int enable = 1;
        if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable)) < 0) {
            ::close(fd);
            throw std::runtime_error("setsockopt(CAN_RAW_FD_FRAMES) failed: " + std::string(std::strerror(errno)));
        }
    }

    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        ::close(fd);
        throw std::runtime_error("Unknown CAN interface '" + ifname + "': " + std::string(std::strerror(errno)));
    }

    struct sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("bind(" + ifname + ") failed: " + std::string(std::strerror(errno)));
    }

    return fd;
}

class EcuProtocolBridge {
public:
    EcuProtocolBridge(const Options& opts, int canFd, Stats& stats)
        : opts_(opts), canFd_(canFd), stats_(stats) {}

    ~EcuProtocolBridge() {
        disconnect();
    }

    bool isConnected() const { return tcpFd_ >= 0 && channel_.has_value(); }
    int tcpFd() const { return tcpFd_; }
    void processPendingPdus() { drainPduQueue(); }

    bool connectAndInitialize() {
        disconnect();

        std::string host;
        uint16_t port = 0;
        if (!parseEndpoint(opts_.endpoint, host, port)) {
            std::cerr << "Invalid endpoint: " << opts_.endpoint << "\n";
            return false;
        }

        tcpFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (tcpFd_ < 0) {
            std::cerr << "TCP socket() failed: " << std::strerror(errno) << "\n";
            return false;
        }

        int flags = ::fcntl(tcpFd_, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(tcpFd_, F_SETFL, flags | O_NONBLOCK);
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            std::cerr << "Invalid endpoint host IP: " << host << "\n";
            disconnect();
            return false;
        }

        int rc = ::connect(tcpFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc < 0 && errno != EINPROGRESS) {
            std::cerr << "connect(" << opts_.endpoint << ") failed: " << std::strerror(errno) << "\n";
            disconnect();
            return false;
        }

        if (rc < 0 && !waitSocketWritable(opts_.connectTimeoutMs)) {
            std::cerr << "connect timeout to " << opts_.endpoint << "\n";
            disconnect();
            return false;
        }

        int one = 1;
        ::setsockopt(tcpFd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (!sendPdu(CANyonero::PDU::requestInfo())) {
            std::cerr << "Failed to request device info\n";
            disconnect();
            return false;
        }

        auto infoPdu = waitForType(CANyonero::PDUType::info, opts_.commandTimeoutMs);
        if (!infoPdu.has_value()) {
            std::cerr << "Timed out waiting for info PDU\n";
            disconnect();
            return false;
        }

        if (opts_.verbose) {
            auto info = infoPdu->information();
            std::cerr << "Connected to " << info.vendor << " " << info.model << " FW=" << info.firmware << "\n";
        }

        uint8_t rxCode = CANyonero::PDU::separationTimeCodeFromMicroseconds(opts_.rxStminUs);
        uint8_t txCode = CANyonero::PDU::separationTimeCodeFromMicroseconds(opts_.txStminUs);

        CANyonero::PDU open = opts_.fd
            ? CANyonero::PDU::openFDChannel(CANyonero::ChannelProtocol::raw_fd, opts_.bitrate, opts_.dataBitrate, rxCode, txCode)
            : CANyonero::PDU::openChannel(CANyonero::ChannelProtocol::raw, opts_.bitrate, rxCode, txCode);

        if (!sendPdu(open)) {
            std::cerr << "Failed to send openChannel\n";
            disconnect();
            return false;
        }

        auto opened = waitForType(CANyonero::PDUType::channelOpened, opts_.commandTimeoutMs);
        if (!opened.has_value()) {
            std::cerr << "Timed out waiting for channelOpened\n";
            disconnect();
            return false;
        }

        const auto openedPayload = opened->payload();
        if (openedPayload.size() < 1) {
            std::cerr << "Invalid channelOpened payload\n";
            disconnect();
            return false;
        }
        channel_ = openedPayload[0];
        std::cerr << "ECU channel opened: " << static_cast<unsigned>(*channel_) << "\n";

        uint32_t requestId = opts_.fixedRequestId.value_or(0);
        if (!setArbitration(requestId)) {
            std::cerr << "Initial setArbitration failed\n";
            disconnect();
            return false;
        }

        std::cerr << "Bridge ready: SocketCAN " << opts_.canIf << " <-> " << opts_.endpoint << "\n";
        return true;
    }

    void disconnect() {
        channel_.reset();
        currentRequestId_.reset();
        rxQueue_.clear();
        if (tcpFd_ >= 0) {
            ::close(tcpFd_);
            tcpFd_ = -1;
        }
    }

    bool handleTcpReadable() {
        uint8_t buffer[4096];
        ssize_t n = ::recv(tcpFd_, buffer, sizeof(buffer), 0);
        if (n == 0) {
            std::cerr << "Adapter closed TCP connection\n";
            return false;
        }
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                return true;
            }
            std::cerr << "TCP recv() failed: " << std::strerror(errno) << "\n";
            return false;
        }

        try {
            auto pdus = stream_.feed(buffer, static_cast<size_t>(n));
            for (auto& pdu : pdus) {
                rxQueue_.push_back(std::move(pdu));
            }
        } catch (const std::exception& e) {
            std::cerr << "PDU parse error: " << e.what() << "\n";
            stats_.protocolErrors++;
            return false;
        }

        return true;
    }

    bool sendCanToEcu(uint32_t canId, bool extended, const uint8_t* data, size_t len) {
        if (!isConnected()) {
            return false;
        }
        if (!channel_.has_value()) {
            return false;
        }

        if (!opts_.fd && len > CAN_MAX_DLEN) {
            stats_.droppedTooLarge++;
            if (opts_.verbose) {
                std::cerr << "Dropping CAN FD frame while not in --fd mode id=" << hexId(canId) << " len=" << len << "\n";
            }
            return true;
        }

        if (len > CANFD_MAX_DLEN) {
            stats_.droppedTooLarge++;
            return true;
        }

        uint32_t requestId = opts_.fixedRequestId.has_value() ? *opts_.fixedRequestId : canId;
        if (requestId > 0x1FFFFFFF) {
            stats_.droppedTooLarge++;
            return true;
        }

        if (!currentRequestId_.has_value() || *currentRequestId_ != requestId) {
            if (!setArbitration(requestId)) {
                return false;
            }
        }

        std::vector<uint8_t> payload(data, data + len);
        if (!sendPdu(CANyonero::PDU::send(*channel_, payload))) {
            return false;
        }

        stats_.ecuTx++;

        if (opts_.verbose) {
            std::cerr << "ECU << CAN id=" << hexId(canId)
                      << (extended ? " ext" : " std")
                      << " len=" << len << "\n";
        }

        return true;
    }

private:
    bool waitSocketWritable(int timeoutMs) {
        pollfd pfd {};
        pfd.fd = tcpFd_;
        pfd.events = POLLOUT;
        int rc = ::poll(&pfd, 1, timeoutMs);
        if (rc <= 0) {
            return false;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(tcpFd_, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
            return false;
        }
        return err == 0;
    }

    bool waitSocketReadable(int timeoutMs) {
        pollfd pfd {};
        pfd.fd = tcpFd_;
        pfd.events = POLLIN;
        int rc = ::poll(&pfd, 1, timeoutMs);
        if (rc <= 0) {
            return false;
        }
        return (pfd.revents & (POLLIN | POLLERR | POLLHUP)) != 0;
    }

    bool sendPdu(const CANyonero::PDU& pdu) {
        if (tcpFd_ < 0) {
            return false;
        }
        auto frame = pdu.frame();
        size_t offset = 0;
        while (offset < frame.size()) {
            ssize_t n = ::send(tcpFd_, frame.data() + offset, frame.size() - offset, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "TCP send() failed: " << std::strerror(errno) << "\n";
                return false;
            }
            offset += static_cast<size_t>(n);
        }
        return true;
    }

    bool setArbitration(uint32_t requestId) {
        if (!channel_.has_value()) {
            return false;
        }

        CANyonero::Arbitration arb{};
        arb.request = requestId;
        arb.requestExtension = 0;
        arb.replyPattern = opts_.replyPattern;
        arb.replyMask = opts_.replyMask;
        arb.replyExtension = 0;

        if (!sendPdu(CANyonero::PDU::setArbitration(*channel_, arb))) {
            std::cerr << "Failed to send setArbitration\n";
            return false;
        }

        auto response = waitForType(CANyonero::PDUType::ok, opts_.commandTimeoutMs);
        if (!response.has_value()) {
            std::cerr << "Timed out waiting for setArbitration OK\n";
            return false;
        }

        currentRequestId_ = requestId;
        if (opts_.verbose) {
            std::cerr << "Arbitration: request=" << hexId(requestId)
                      << " replyPattern=" << hexId(opts_.replyPattern)
                      << " replyMask=0x" << std::uppercase << std::hex << opts_.replyMask << std::dec << "\n";
        }

        return true;
    }

    std::optional<CANyonero::PDU> waitForType(CANyonero::PDUType type, int timeoutMs) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            while (!rxQueue_.empty()) {
                CANyonero::PDU pdu = std::move(rxQueue_.front());
                rxQueue_.pop_front();
                if (pdu.type() == type) {
                    return pdu;
                }
                handlePdu(std::move(pdu));
            }

            auto now = std::chrono::steady_clock::now();
            int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            if (remaining < 1) {
                remaining = 1;
            }

            if (!waitSocketReadable(remaining)) {
                continue;
            }

            if (!handleTcpReadable()) {
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    static uint32_t readU32BE(const std::vector<uint8_t>& payload, size_t off) {
        return (static_cast<uint32_t>(payload[off]) << 24) |
               (static_cast<uint32_t>(payload[off + 1]) << 16) |
               (static_cast<uint32_t>(payload[off + 2]) << 8) |
               static_cast<uint32_t>(payload[off + 3]);
    }

    void drainPduQueue() {
        while (!rxQueue_.empty()) {
            CANyonero::PDU pdu = std::move(rxQueue_.front());
            rxQueue_.pop_front();
            handlePdu(std::move(pdu));
        }
    }

    void handlePdu(CANyonero::PDU pdu) {
        switch (pdu.type()) {
            case CANyonero::PDUType::received:
            case CANyonero::PDUType::receivedCompressed: {
                const auto payload = pdu.payload();
                if (payload.size() < 6) {
                    stats_.protocolErrors++;
                    return;
                }

                uint32_t id = readU32BE(payload, 1);
                bool extended = id > 0x7FF;

                std::vector<uint8_t> data =
                    pdu.type() == CANyonero::PDUType::received ? pdu.data() : pdu.uncompressedData();

                if (data.size() > CANFD_MAX_DLEN) {
                    stats_.droppedTooLarge++;
                    return;
                }

                if (!opts_.fd && data.size() > CAN_MAX_DLEN) {
                    stats_.droppedTooLarge++;
                    if (opts_.verbose) {
                        std::cerr << "Dropping incoming FD frame without --fd id=" << hexId(id)
                                  << " len=" << data.size() << "\n";
                    }
                    return;
                }

                if (!writeCanFrame(id, extended, data)) {
                    std::cerr << "Failed to write SocketCAN frame\n";
                } else {
                    stats_.canTx++;
                    stats_.ecuRx++;
                    if (opts_.verbose) {
                        std::cerr << "CAN << ECU id=" << hexId(id)
                                  << (extended ? " ext" : " std")
                                  << " len=" << data.size() << "\n";
                    }
                }
                break;
            }

            case CANyonero::PDUType::ok:
            case CANyonero::PDUType::pong:
            case CANyonero::PDUType::info:
            case CANyonero::PDUType::voltage:
            case CANyonero::PDUType::channelOpened:
            case CANyonero::PDUType::channelClosed:
                break;

            case CANyonero::PDUType::errorUnspecified:
            case CANyonero::PDUType::errorHardware:
            case CANyonero::PDUType::errorInvalidChannel:
            case CANyonero::PDUType::errorInvalidPeriodic:
            case CANyonero::PDUType::errorNoResponse:
            case CANyonero::PDUType::errorInvalidRPC:
            case CANyonero::PDUType::errorInvalidCommand:
                stats_.ecuErrors++;
                std::cerr << "Adapter returned error PDU type=0x"
                          << std::uppercase << std::hex << static_cast<unsigned>(pdu.type()) << std::dec << "\n";
                break;

            default:
                if (opts_.verbose) {
                    std::cerr << "Ignoring PDU type=0x"
                              << std::uppercase << std::hex << static_cast<unsigned>(pdu.type()) << std::dec << "\n";
                }
                break;
        }
    }

    bool writeCanFrame(uint32_t id, bool extended, const std::vector<uint8_t>& data) {
        uint32_t canId = id;
        if (extended) {
            canId |= CAN_EFF_FLAG;
        }

        if (data.size() > CAN_MAX_DLEN) {
            struct canfd_frame frame {};
            frame.can_id = canId;
            frame.len = static_cast<__u8>(data.size());
            std::memcpy(frame.data, data.data(), data.size());

            ssize_t written = ::write(canFd_, &frame, CANFD_MTU);
            if (written != CANFD_MTU) {
                return false;
            }
            return true;
        }

        struct can_frame frame {};
        frame.can_id = canId;
        frame.can_dlc = static_cast<__u8>(data.size());
        std::memcpy(frame.data, data.data(), data.size());

        ssize_t written = ::write(canFd_, &frame, CAN_MTU);
        return written == CAN_MTU;
    }

    const Options& opts_;
    int canFd_ = -1;
    int tcpFd_ = -1;
    Stats& stats_;
    PDUStream stream_;
    std::deque<CANyonero::PDU> rxQueue_;
    std::optional<CANyonero::ChannelHandle> channel_;
    std::optional<uint32_t> currentRequestId_;
};

void printStats(const Stats& s) {
    std::cerr
        << "stats: can_rx=" << s.canRx
        << " can_tx=" << s.canTx
        << " ecu_rx=" << s.ecuRx
        << " ecu_tx=" << s.ecuTx
        << " dropped_no_link=" << s.droppedNoLink
        << " dropped_too_large=" << s.droppedTooLarge
        << " protocol_errors=" << s.protocolErrors
        << " ecu_errors=" << s.ecuErrors
        << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    ::signal(SIGINT, onSignal);
    ::signal(SIGTERM, onSignal);

    try {
        Options opts = parseArgs(argc, argv);

        std::cerr
            << "Starting bridge with endpoint=" << opts.endpoint
            << " can_if=" << opts.canIf
            << " mode=" << (opts.fd ? "raw_fd" : "raw")
            << " bitrate=" << opts.bitrate;
        if (opts.fd) {
            std::cerr << " data_bitrate=" << opts.dataBitrate;
        }
        std::cerr << "\n";

        int canFd = openSocketCan(opts.canIf, opts.fd, opts.recvOwn);

        Stats stats;
        EcuProtocolBridge bridge(opts, canFd, stats);

        auto nextReconnect = std::chrono::steady_clock::now();
        auto nextStats = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, opts.statsIntervalSec));

        while (gRunning) {
            if (!bridge.isConnected()) {
                auto now = std::chrono::steady_clock::now();
                if (now >= nextReconnect) {
                    std::cerr << "Connecting to adapter...\n";
                    if (!bridge.connectAndInitialize()) {
                        nextReconnect = now + std::chrono::milliseconds(opts.reconnectMs);
                    }
                }
            }

            std::vector<pollfd> fds;
            fds.push_back(pollfd{canFd, POLLIN, 0});
            if (bridge.isConnected()) {
                fds.push_back(pollfd{bridge.tcpFd(), POLLIN, 0});
            }

            int rc = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), kPollIntervalMs);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "poll() failed: " << std::strerror(errno) << "\n";
                break;
            }

            if (rc > 0) {
                if (fds[0].revents & POLLIN) {
                    struct canfd_frame fdFrame {};
                    ssize_t n = ::read(canFd, &fdFrame, sizeof(fdFrame));
                    if (n == CAN_MTU || n == CANFD_MTU) {
                        bool isFd = (n == CANFD_MTU);
                        uint32_t rawId = fdFrame.can_id;
                        bool isErr = (rawId & CAN_ERR_FLAG) != 0;
                        bool isRtr = (rawId & CAN_RTR_FLAG) != 0;
                        if (!isErr && !isRtr) {
                            bool extended = (rawId & CAN_EFF_FLAG) != 0;
                            uint32_t id = rawId & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
                            size_t len = isFd ? fdFrame.len : static_cast<size_t>(reinterpret_cast<can_frame*>(&fdFrame)->can_dlc);
                            const uint8_t* data = fdFrame.data;

                            stats.canRx++;
                            if (bridge.isConnected()) {
                                if (!bridge.sendCanToEcu(id, extended, data, len)) {
                                    std::cerr << "Bridge send failed, reconnecting...\n";
                                    bridge.disconnect();
                                }
                            } else {
                                stats.droppedNoLink++;
                            }
                        }
                    }
                }

                if (bridge.isConnected() && fds.size() > 1 && (fds[1].revents & (POLLIN | POLLERR | POLLHUP))) {
                    if (!bridge.handleTcpReadable()) {
                        bridge.disconnect();
                        nextReconnect = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.reconnectMs);
                    } else {
                        bridge.processPendingPdus();
                    }
                }
            }

            if (opts.statsIntervalSec > 0) {
                auto now = std::chrono::steady_clock::now();
                if (now >= nextStats) {
                    printStats(stats);
                    nextStats = now + std::chrono::seconds(opts.statsIntervalSec);
                }
            }
        }

        std::cerr << "Stopping bridge...\n";
        printStats(stats);
        ::close(canFd);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
