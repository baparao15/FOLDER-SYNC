#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sync {

void ensureDirectory(const std::filesystem::path& path);

std::string relativePathOf(const std::filesystem::path& root,
                           const std::filesystem::path& fullPath);

std::vector<char> readFileBytes(const std::filesystem::path& filePath);
bool writeFileBytes(const std::filesystem::path& filePath,
                    const std::vector<char>& data);

} // namespace sync

