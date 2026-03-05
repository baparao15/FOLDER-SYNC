#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "shared/protocol.h"

class DirectoryWatcher {
public:
    explicit DirectoryWatcher(std::filesystem::path root);

    std::vector<sync::FileEvent> collectChanges();
    // Mark a path as already synced/applied so the watcher won't report it
    // as a new local change (used after applying a remote event).
    void markPathAsSynced(const std::string& relative, bool deleted = false);

private:
    using Snapshot = std::unordered_map<std::string, std::filesystem::file_time_type>;

    void visitCurrentState(Snapshot& current, std::vector<sync::FileEvent>& events);
    void detectDeletions(const Snapshot& current, std::vector<sync::FileEvent>& events);

    std::filesystem::path root_;
    Snapshot snapshot_;
    mutable std::mutex mutex_;
};
