#include "shared/browser_launcher.h"
#include "shared/logger.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

namespace sync {

bool launchBrowser(const std::string& url) {
#ifdef _WIN32
    // Windows: Use ShellExecute to open URL in default browser
    HINSTANCE result = ShellExecuteA(
        nullptr,           // hwnd
        "open",           // operation
        url.c_str(),      // file/URL
        nullptr,          // parameters
        nullptr,          // directory
        SW_SHOWNORMAL     // show command
    );
    
    // ShellExecute returns a value greater than 32 on success
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        LOG_INFO("Browser launched successfully: " + url);
        return true;
    } else {
        LOG_WARNING("Failed to launch browser for: " + url);
        return false;
    }
#else
    // Linux/Mac: Use xdg-open or open command
    std::string command;
    #ifdef __APPLE__
        command = "open \"" + url + "\"";
    #else
        command = "xdg-open \"" + url + "\" &";
    #endif
    
    int result = std::system(command.c_str());
    if (result == 0) {
        LOG_INFO("Browser launched successfully: " + url);
        return true;
    } else {
        LOG_WARNING("Failed to launch browser for: " + url);
        return false;
    }
#endif
}

} // namespace sync
