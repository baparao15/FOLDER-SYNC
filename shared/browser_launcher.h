#pragma once

#include <string>

namespace sync {

/**
 * Launch the default web browser to the specified URL
 * @param url The URL to open
 * @return true if successful, false otherwise
 */
bool launchBrowser(const std::string& url);

} // namespace sync
