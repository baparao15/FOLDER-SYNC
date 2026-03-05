#include "client/watcher.h"

#include <filesystem>
#include <vector>

#include "shared/utils.h"

namespace fs = std::filesystem;

DirectoryWatcher::DirectoryWatcher(fs::path root) : root_(std::move(root)) {
    sync::ensureDirectory(root_);
}

std::vector<sync::FileEvent> DirectoryWatcher::collectChanges() {
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot current;
    std::vector<sync::FileEvent> events;

    visitCurrentState(current, events);
    detectDeletions(current, events);

    snapshot_ = std::move(current);
    return events;
}

void DirectoryWatcher::visitCurrentState(Snapshot& current, std::vector<sync::FileEvent>& events) {
    if (!fs::exists(root_)) {
        return;
    }

    // Process both directories and files in a single pass
    for (const auto& entry : fs::recursive_directory_iterator(root_, 
            fs::directory_options::skip_permission_denied)) {
        const auto relative = sync::relativePathOf(root_, entry.path());
        const auto lastWrite = fs::last_write_time(entry);
        
        current.emplace(relative, lastWrite);
        const auto prev = snapshot_.find(relative);
        
        const bool isNew = (prev == snapshot_.end());
        const bool isModified = !isNew && prev->second != lastWrite;
        
        if (entry.is_directory()) {
            // Handle directory
            if (isNew) {
                sync::FileEvent event;
                event.type = sync::OperationType::Create;
                event.relativePath = relative;
                event.payload.clear(); // Empty payload indicates directory
                events.emplace_back(std::move(event));
            }
        } else if (entry.is_regular_file()) {
            // Handle file
            if (isNew || isModified) {
                sync::FileEvent event;
                event.type = isNew ? sync::OperationType::Create : sync::OperationType::Update;
                event.relativePath = relative;
                event.payload = sync::readFileBytes(entry.path());
                events.emplace_back(std::move(event));
            }
        }
    }
}

void DirectoryWatcher::detectDeletions(const Snapshot& current, std::vector<sync::FileEvent>& events) {
    for (const auto& [path, _] : snapshot_) {
        if (current.find(path) == current.end()) {
            sync::FileEvent event;
            event.type = sync::OperationType::Delete;
            event.relativePath = path;
            events.emplace_back(std::move(event));
        }
    }
}

void DirectoryWatcher::markPathAsSynced(const std::string& relative, bool deleted) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        if (deleted) {
            snapshot_.erase(relative);
            return;
        }
        const auto full = root_ / relative;
        if (!fs::exists(full)) {
            snapshot_.erase(relative);
            return;
        }
        const auto lastWrite = fs::last_write_time(full);
        snapshot_[relative] = lastWrite;
    } catch (...) {
        // Ignore errors while marking snapshot
    }
}

