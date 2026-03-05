#include "client/sender.h"

NetworkSender::NetworkSender(std::string host, std::uint16_t port)
    : writer_(std::move(host), port) {}

bool NetworkSender::connect() {
    return writer_.connect();
}

bool NetworkSender::send(const sync::FileEvent& event) {
    return writer_.sendEvent(event);
}

bool NetworkSender::sendBatch(const std::vector<sync::FileEvent>& events) {
    for (const auto& event : events) {
        if (!send(event)) {
            return false;
        }
    }
    return true;
}

bool NetworkSender::receive(sync::FileEvent& event) {
    return writer_.readEvent(event);
}

