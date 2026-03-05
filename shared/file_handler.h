#pragma once

#include <filesystem>

#include "shared/protocol.h"

class FileHandler {
public:
    explicit FileHandler(std::filesystem::path targetRoot);

    bool apply(const sync::FileEvent& event);

private:
    std::filesystem::path fullPathFor(const std::string& relative) const;

    std::filesystem::path root_;
};
