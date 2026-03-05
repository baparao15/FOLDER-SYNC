#pragma once

#include <string>
#include <vector>

#include <cstdint>

namespace sync {

enum class OperationType {
    Create,
    Update,
    Delete
};

struct FileEvent {
    OperationType type{};
    std::string relativePath;
    std::vector<char> payload; // empty for DELETE
    std::uint32_t peerId{0};   // ID of peer that made this change (0 = local)
};

std::string operationToString(OperationType type);
OperationType stringToOperation(const std::string& value);

class NetworkWriter {
public:
    NetworkWriter(std::string host, std::uint16_t port);
    ~NetworkWriter();

    bool connect();
    bool sendEvent(const FileEvent& event);
    void close();
    // Read an incoming event from the connected socket (blocking).
    bool readEvent(FileEvent& event);

private:
    bool writeLine(const std::string& line);
    bool writeBytes(const std::vector<char>& data);

    std::string host_;
    std::uint16_t port_;
    long long socket_; // platform-specific handle stored as integer
};

class NetworkReader {
public:
    explicit NetworkReader(std::uint16_t port);
    ~NetworkReader();

    bool open();  // waits for client connection
    bool readEvent(FileEvent& event);
    void close();
    // Send an event on the currently accepted client socket (server -> client)
    bool sendEvent(const FileEvent& event);

private:
    bool acceptClient();
    bool readLine(std::string& line);
    bool readBytes(std::vector<char>& buffer, std::size_t size);

    std::uint16_t port_;
    long long listenSocket_; // platform-specific handle stored as integer
    long long clientSocket_;
};

} // namespace sync

