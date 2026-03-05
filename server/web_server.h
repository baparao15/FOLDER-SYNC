#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

namespace sync {

struct SyncStatus {
    bool isRunning{false};
    std::string syncFolder;
    int connectedClients{0};
    std::vector<std::string> clientIPs;
    long long totalFilesSynced{0};
    long long totalBytesSynced{0};
    std::string lastSyncTime;
    std::vector<std::string> recentLogs;
};

struct ClientConnection {
    std::string ipAddress;
    std::uint16_t port;
    bool isConnected{false};
    std::string lastSeen;
};

class WebServer {
public:
    WebServer(std::uint16_t port, const std::string& webRoot);
    ~WebServer();

    // Start the web server in a background thread
    bool start();
    
    // Stop the web server
    void stop();
    
    // Check if server is running
    bool isRunning() const;
    
    // Update sync status (called by main sync logic)
    void updateStatus(const SyncStatus& status);
    
    // Get current status
    SyncStatus getStatus() const;
    
    // Add a client connection
    void addClient(const std::string& ip, std::uint16_t port);
    
    // Remove a client connection
    void removeClient(const std::string& ip);
    
    // Get all clients
    std::vector<ClientConnection> getClients() const;
    
    // Add log entry
    void addLog(const std::string& message);
    
    // Set callback for configuration changes
    void setConfigCallback(std::function<void(const std::string&)> callback);
    
    // Set callback for client connection requests
    void setConnectCallback(std::function<bool(const std::string&, std::uint16_t)> callback);
    
    // Set callback for role selection (server/client)
    void setRoleCallback(std::function<void(const std::string&)> callback);
    
    // Set callback for starting sync (role, folder, serverIP, serverPort)
    void setStartCallback(std::function<void(const std::string&, const std::string&, const std::string&, std::uint16_t)> callback);
    
    // Get current role and initialization status
    std::string getCurrentRole() const;
    bool isInitialized() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    std::uint16_t port_;
    std::string webRoot_;
    std::atomic<bool> running_{false};
    
    mutable std::mutex statusMutex_;
    SyncStatus status_;
    
    mutable std::mutex clientsMutex_;
    std::vector<ClientConnection> clients_;
    
    mutable std::mutex logsMutex_;
    std::vector<std::string> logs_;
    static constexpr size_t MAX_LOGS = 100;
    
    std::function<void(const std::string&)> configCallback_;
    std::function<bool(const std::string&, std::uint16_t)> connectCallback_;
    std::function<void(const std::string&)> roleCallback_;
    std::function<void(const std::string&, const std::string&, const std::string&, std::uint16_t)> startCallback_;
    
    std::string currentRole_{"unset"};
    std::atomic<bool> initialized_{false};
};

} // namespace sync
