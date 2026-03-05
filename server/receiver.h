#pragma once

#include <cstdint>

#include "shared/protocol.h"

class NetworkReceiver {
public:
    explicit NetworkReceiver(std::uint16_t port);

    bool open();
    bool next(sync::FileEvent& event);
    // Send an event back to the connected client
    bool send(const sync::FileEvent& event);

private:
    sync::NetworkReader reader_;
};

