#include "shared/file_handler.h"

#include <filesystem>

#include "shared/logger.h"
#include "shared/utils.h"

namespace fs = std::filesystem;

FileHandler::FileHandler(fs::path targetRoot) : root_(std::move(targetRoot)) {
    sync::ensureDirectory(root_);
}

bool FileHandler::apply(const sync::FileEvent& event) {
    const auto destination = fullPathFor(event.relativePath);
    
    switch (event.type) {
        case sync::OperationType::Create:
        case sync::OperationType::Update: {
            // Check if destination already exists and what type it is
            if (fs::exists(destination)) {
                if (fs::is_directory(destination) && !event.payload.empty()) {
                    // Destination is a directory but we're trying to write a file
                    // Remove directory first
                    try {
                        fs::remove_all(destination);
                    } catch (...) {
                        // Ignore errors, will fail on writeFileBytes if needed
                    }
                } else if (fs::is_regular_file(destination) && event.payload.empty()) {
                    // Destination is a file but we're trying to create a directory
                    // This shouldn't happen, but skip to avoid error
                    LOG_WARNING("Skipping directory creation for existing file: " + destination.string());
                    return false;
                }
            }
            
            // Determine if this is a directory or file
            // Rule: If path has an extension, it's always a file (even if empty)
            //       If path has no extension and payload is empty, it's a directory
            bool isDirectory = event.payload.empty() && !destination.has_extension();
            
            if (isDirectory) {
                // Create directory
                try {
                    // Only create if it doesn't exist or is not a file
                    if (fs::exists(destination)) {
                        if (fs::is_regular_file(destination)) {
                            LOG_WARNING("Cannot create directory, file exists: " + destination.string());
                            return false;
                        }
                        // Already exists as directory, skip
                        return true;
                    }
                    fs::create_directories(destination);
                    LOG_INFO("Created directory: " + destination.string());
                    return true;
                } catch (const std::exception& ex) {
                    LOG_ERROR("Failed to create directory: " + destination.string() + " - " + ex.what());
                    return false;
                }
            } else {
                // Create/update file (even if payload is empty - empty file)
                // Create/update file - ensure parent directory exists
                try {
                    if (destination.has_parent_path()) {
                        fs::create_directories(destination.parent_path());
                    }
                } catch (...) {
                    // Ignore parent directory creation errors
                }
                
                if (!sync::writeFileBytes(destination, event.payload)) {
                    LOG_ERROR("Failed to write file: " + destination.string());
                    return false;
                }
                LOG_INFO("Synced file: " + destination.string());
                return true;
            }
            return false;
        }
        case sync::OperationType::Delete:
            if (fs::exists(destination)) {
                try {
                    if (fs::is_directory(destination)) {
                        fs::remove_all(destination);  // Remove directory recursively
                        LOG_INFO("Deleted directory: " + destination.string());
                    } else {
                        fs::remove(destination);  // Remove file
                        LOG_INFO("Deleted file: " + destination.string());
                    }
                    return true;
                } catch (const std::exception& ex) {
                    LOG_ERROR("Failed to delete: " + destination.string() + " - " + ex.what());
                    return false;
                }
            }
            return true;
    }
    return false;
}

fs::path FileHandler::fullPathFor(const std::string& relative) const {
    return root_ / relative;
}
