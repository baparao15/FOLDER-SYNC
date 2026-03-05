#include "server/receiver.h"

NetworkReceiver::NetworkReceiver(std::uint16_t port)
    : reader_(port) {}

bool NetworkReceiver::open() {
    return reader_.open();
}

bool NetworkReceiver::next(sync::FileEvent& event) {
    return reader_.readEvent(event);
}

bool NetworkReceiver::send(const sync::FileEvent& event) {
    return reader_.sendEvent(event);
}

