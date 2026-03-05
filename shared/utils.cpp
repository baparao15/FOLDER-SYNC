#include "shared/utils.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace sync {

void ensureDirectory(const fs::path& path) {
    if (path.empty()) {
        return;
    }
    fs::create_directories(path);
}

std::string relativePathOf(const fs::path& root, const fs::path& fullPath) {
    return fs::relative(fullPath, root).generic_string();
}

std::vector<char> readFileBytes(const fs::path& filePath) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file for reading: " + filePath.string());
    }
    return std::vector<char>(std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>());
}

bool writeFileBytes(const fs::path& filePath, const std::vector<char>& data) {
    ensureDirectory(filePath.parent_path());
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(output);
}

} // namespace sync

