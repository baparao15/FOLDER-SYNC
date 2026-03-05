#include "shared/protocol.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sync {
namespace {

#ifdef _WIN32
using SocketType = SOCKET;
const SocketType kInvalidSocket = INVALID_SOCKET;
#else
using SocketType = int;
const SocketType kInvalidSocket = -1;
#endif

bool initializeSockets() {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return false;
        }
        initialized = true;
    }
#endif
    return true;
}

void closeSocket(SocketType& socket) {
    if (socket != kInvalidSocket) {
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
        socket = kInvalidSocket;
    }
}

bool sendAll(SocketType socket, const char* data, std::size_t size) {
    while (size > 0) {
        int sent = send(socket, data, static_cast<int>(size), 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool recvAll(SocketType socket, char* buffer, std::size_t size) {
    std::size_t total = 0;
    while (total < size) {
        int received = recv(socket, buffer + total, static_cast<int>(size - total), 0);
        if (received <= 0) {
            return false;
        }
        total += static_cast<std::size_t>(received);
    }
    return true;
}

bool recvLine(SocketType socket, std::string& line) {
    line.clear();
    char ch;
    while (true) {
        int received = recv(socket, &ch, 1, 0);
        if (received <= 0) {
            return false;
        }
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            line.push_back(ch);
        }
    }
    return true;
}

bool sendLine(SocketType socket, const std::string& line) {
    std::string data = line;
    data.push_back('\n');
    return sendAll(socket, data.c_str(), data.size());
}

SocketType createClientSocket(const std::string& host, std::uint16_t port) {
    if (!initializeSockets()) {
        return kInvalidSocket;
    }

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    const std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
        return kInvalidSocket;
    }

    SocketType client = kInvalidSocket;
    for (auto ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        client = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (client == kInvalidSocket) {
            continue;
        }
        if (connect(client, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) {
            break;
        }
        closeSocket(client);
    }

    freeaddrinfo(result);
    return client;
}

SocketType createServerSocket(std::uint16_t port) {
    if (!initializeSockets()) {
        return kInvalidSocket;
    }

    SocketType server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == kInvalidSocket) {
        return kInvalidSocket;
    }

    int enable = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enable), sizeof(enable));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#endif

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closeSocket(server);
        return kInvalidSocket;
    }

    if (listen(server, 1) < 0) {
        closeSocket(server);
        return kInvalidSocket;
    }

    return server;
}

} // namespace

std::string operationToString(OperationType type) {
    switch (type) {
        case OperationType::Create: return "CREATE";
        case OperationType::Update: return "UPDATE";
        case OperationType::Delete: return "DELETE";
    }
    return "UNKNOWN";
}

OperationType stringToOperation(const std::string& value) {
    if (value == "CREATE") {
        return OperationType::Create;
    }
    if (value == "UPDATE") {
        return OperationType::Update;
    }
    return OperationType::Delete;
}

NetworkWriter::NetworkWriter(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port), socket_(kInvalidSocket) {}

NetworkWriter::~NetworkWriter() {
    close();
}

bool NetworkWriter::connect() {
    if (socket_ != kInvalidSocket) {
        return true;
    }
    SocketType sock = createClientSocket(host_, port_);
    if (sock == kInvalidSocket) {
        return false;
    }
    socket_ = static_cast<long long>(sock);
    return true;
}

bool NetworkWriter::sendEvent(const FileEvent& event) {
    if (socket_ == kInvalidSocket && !connect()) {
        return false;
    }
    SocketType sock = static_cast<SocketType>(socket_);
    if (!sendLine(sock, operationToString(event.type))) {
        close();
        return false;
    }
    if (!sendLine(sock, event.relativePath)) {
        close();
        return false;
    }
    if (!sendLine(sock, std::to_string(event.peerId))) {
        close();
        return false;
    }
    if (event.type == OperationType::Create || event.type == OperationType::Update) {
        if (!sendLine(sock, std::to_string(event.payload.size()))) {
            close();
            return false;
        }
        if (!writeBytes(event.payload)) {
            close();
            return false;
        }
    }
    return true;
}

bool NetworkWriter::readEvent(FileEvent& event) {
    if (socket_ == kInvalidSocket) return false;
    SocketType sock = static_cast<SocketType>(socket_);

    std::string typeLine;
    if (!recvLine(sock, typeLine)) {
        close();
        return false;
    }
    event.type = stringToOperation(typeLine);

    std::string pathLine;
    if (!recvLine(sock, pathLine)) {
        close();
        return false;
    }
    event.relativePath = std::move(pathLine);

    std::string peerIdLine;
    if (!recvLine(sock, peerIdLine)) {
        close();
        return false;
    }
    try {
        event.peerId = static_cast<std::uint32_t>(std::stoul(peerIdLine));
    } catch (...) {
        event.peerId = 0;
    }

    if (event.type == OperationType::Create || event.type == OperationType::Update) {
        std::string sizeLine;
        if (!recvLine(sock, sizeLine)) {
            close();
            return false;
        }
        const std::size_t payloadSize = static_cast<std::size_t>(std::stoll(sizeLine));
        event.payload.assign(payloadSize, '\0');
        if (!recvAll(sock, event.payload.data(), payloadSize)) {
            close();
            return false;
        }
    } else {
        event.payload.clear();
    }

    return true;
}

void NetworkWriter::close() {
    SocketType sock = static_cast<SocketType>(socket_);
    closeSocket(sock);
    socket_ = kInvalidSocket;
}

bool NetworkWriter::writeLine(const std::string& line) {
    SocketType sock = static_cast<SocketType>(socket_);
    return sendLine(sock, line);
}

bool NetworkWriter::writeBytes(const std::vector<char>& data) {
    if (data.empty()) {
        return true;
    }
    SocketType sock = static_cast<SocketType>(socket_);
    return sendAll(sock, data.data(), data.size());
}

NetworkReader::NetworkReader(std::uint16_t port)
    : port_(port), listenSocket_(kInvalidSocket), clientSocket_(kInvalidSocket) {}

NetworkReader::~NetworkReader() {
    close();
    SocketType listenSock = static_cast<SocketType>(listenSocket_);
    closeSocket(listenSock);
}

bool NetworkReader::open() {
    if (listenSocket_ == kInvalidSocket) {
        SocketType server = createServerSocket(port_);
        if (server == kInvalidSocket) {
            return false;
        }
        listenSocket_ = static_cast<long long>(server);
    }
    return acceptClient();
}

bool NetworkReader::acceptClient() {
    SocketType listenSock = static_cast<SocketType>(listenSocket_);
    if (listenSock == kInvalidSocket) {
        return false;
    }
    SocketType client = accept(listenSock, nullptr, nullptr);
    if (client == kInvalidSocket) {
        return false;
    }
    close();
    clientSocket_ = static_cast<long long>(client);
    return true;
}

bool NetworkReader::readEvent(FileEvent& event) {
    if (clientSocket_ == kInvalidSocket) {
        if (!open()) {
            return false;
        }
    }
    SocketType sock = static_cast<SocketType>(clientSocket_);

    std::string typeLine;
    if (!recvLine(sock, typeLine)) {
        close();
        return false;
    }
    event.type = stringToOperation(typeLine);

    std::string pathLine;
    if (!recvLine(sock, pathLine)) {
        close();
        return false;
    }
    event.relativePath = std::move(pathLine);

    std::string peerIdLine;
    if (!recvLine(sock, peerIdLine)) {
        close();
        return false;
    }
    try {
        event.peerId = static_cast<std::uint32_t>(std::stoul(peerIdLine));
    } catch (...) {
        event.peerId = 0;
    }

    if (event.type == OperationType::Create || event.type == OperationType::Update) {
        std::string sizeLine;
        if (!recvLine(sock, sizeLine)) {
            close();
            return false;
        }
        const std::size_t payloadSize = static_cast<std::size_t>(std::stoll(sizeLine));
        event.payload.assign(payloadSize, '\0');
        if (!readBytes(event.payload, payloadSize)) {
            close();
            return false;
        }
    } else {
        event.payload.clear();
    }

    return true;
}

bool NetworkReader::sendEvent(const FileEvent& event) {
    if (clientSocket_ == kInvalidSocket) {
        return false;
    }
    SocketType sock = static_cast<SocketType>(clientSocket_);
    if (!sendLine(sock, operationToString(event.type))) {
        close();
        return false;
    }
    if (!sendLine(sock, event.relativePath)) {
        close();
        return false;
    }
    if (!sendLine(sock, std::to_string(event.peerId))) {
        close();
        return false;
    }
    if (event.type == OperationType::Create || event.type == OperationType::Update) {
        if (!sendLine(sock, std::to_string(event.payload.size()))) {
            close();
            return false;
        }
        if (!sendAll(sock, event.payload.data(), event.payload.size())) {
            close();
            return false;
        }
    }
    return true;
}

void NetworkReader::close() {
    SocketType client = static_cast<SocketType>(clientSocket_);
    closeSocket(client);
    clientSocket_ = kInvalidSocket;
}

bool NetworkReader::readLine(std::string& line) {
    SocketType sock = static_cast<SocketType>(clientSocket_);
    return recvLine(sock, line);
}

bool NetworkReader::readBytes(std::vector<char>& buffer, std::size_t size) {
    if (size == 0) {
        buffer.clear();
        return true;
    }
    SocketType sock = static_cast<SocketType>(clientSocket_);
    return recvAll(sock, buffer.data(), size);
}

} // namespace sync

