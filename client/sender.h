#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "shared/protocol.h"

class NetworkSender {
public:
    NetworkSender(std::string host, std::uint16_t port);

    bool connect();  // Public method to connect/reconnect
    bool send(const sync::FileEvent& event);
    bool sendBatch(const std::vector<sync::FileEvent>& events);
    // Blocking receive of an incoming event from server on the same socket
    bool receive(sync::FileEvent& event);

private:
    sync::NetworkWriter writer_;
};

