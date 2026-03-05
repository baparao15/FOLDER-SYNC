#include "server/web_server.h"
#include "shared/httplib.h"
#include "shared/logger.h"

#include <thread>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <filesystem>

namespace sync {

// Pimpl implementation to hide httplib details
class WebServer::Impl {
public:
    std::unique_ptr<httplib::Server> server;
    std::thread serverThread;
};

WebServer::WebServer(std::uint16_t port, const std::string& webRoot)
    : impl_(std::make_unique<Impl>()), port_(port), webRoot_(webRoot) {
    impl_->server = std::make_unique<httplib::Server>();
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    if (running_) {
        return true;
    }

    auto& svr = *impl_->server;

    // Enable CORS for development
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    // Use the provided webRoot path (should be absolute from main.cpp)
    std::filesystem::path absWebRoot(webRoot_);
    
    LOG_INFO("Serving static files from: " + absWebRoot.string());
    
    // Verify the web directory exists
    if (!std::filesystem::exists(absWebRoot)) {
        LOG_ERROR("Web directory does not exist: " + absWebRoot.string());
    }
    
    svr.set_mount_point("/", absWebRoot.string());
    
    // Explicit handler for root path to serve index.html
    svr.Get("/", [absWebRoot](const httplib::Request&, httplib::Response& res) {
        std::filesystem::path indexPath = absWebRoot / "index.html";
        std::ifstream file(indexPath, std::ios::binary);
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            res.set_content(content, "text/html");
        } else {
            res.status = 404;
            res.set_content("index.html not found at: " + indexPath.string(), "text/plain");
        }
    });

    // API: Get status
    svr.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(statusMutex_);
        std::lock_guard<std::mutex> clientLock(clientsMutex_);
        
        std::ostringstream json;
        json << "{"
             << "\"isRunning\":" << (status_.isRunning ? "true" : "false") << ","
             << "\"syncFolder\":\"" << status_.syncFolder << "\","
             << "\"connectedClients\":" << clients_.size() << ","
             << "\"totalFilesSynced\":" << status_.totalFilesSynced << ","
             << "\"totalBytesSynced\":" << status_.totalBytesSynced << ","
             << "\"lastSyncTime\":\"" << status_.lastSyncTime << "\","
             << "\"clients\":[";
        
        for (size_t i = 0; i < clients_.size(); ++i) {
            if (i > 0) json << ",";
            json << "{"
                 << "\"ip\":\"" << clients_[i].ipAddress << "\","
                 << "\"port\":" << clients_[i].port << ","
                 << "\"connected\":" << (clients_[i].isConnected ? "true" : "false") << ","
                 << "\"lastSeen\":\"" << clients_[i].lastSeen << "\""
                 << "}";
        }
        
        json << "]}";
        
        res.set_content(json.str(), "application/json");
    });

    // API: Get logs
    svr.Get("/api/logs", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(logsMutex_);
        
        std::ostringstream json;
        json << "{\"logs\":[";
        
        for (size_t i = 0; i < logs_.size(); ++i) {
            if (i > 0) json << ",";
            // Escape quotes in log messages
            std::string escaped = logs_[i];
            size_t pos = 0;
            while ((pos = escaped.find('"', pos)) != std::string::npos) {
                escaped.replace(pos, 1, "\\\"");
                pos += 2;
            }
            json << "\"" << escaped << "\"";
        }
        
        json << "]}";
        
        res.set_content(json.str(), "application/json");
    });

    // API: Update configuration
    svr.Post("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
        // Simple JSON parsing for folder path
        std::string body = req.body;
        size_t folderPos = body.find("\"folder\":\"");
        if (folderPos != std::string::npos) {
            folderPos += 10; // length of "folder":""
            size_t endPos = body.find("\"", folderPos);
            if (endPos != std::string::npos) {
                std::string folder = body.substr(folderPos, endPos - folderPos);
                
                if (configCallback_) {
                    configCallback_(folder);
                }
                
                res.set_content("{\"success\":true}", "application/json");
                return;
            }
        }
        
        res.set_content("{\"success\":false,\"error\":\"Invalid request\"}", "application/json");
    });

    // API: Connect to client
    svr.Post("/api/connect", [this](const httplib::Request& req, httplib::Response& res) {
        std::string body = req.body;
        
        // Parse IP address
        size_t ipPos = body.find("\"ip\":\"");
        if (ipPos == std::string::npos) {
            res.set_content("{\"success\":false,\"error\":\"Missing IP address\"}", "application/json");
            return;
        }
        ipPos += 6;
        size_t ipEnd = body.find("\"", ipPos);
        std::string ip = body.substr(ipPos, ipEnd - ipPos);
        
        // Parse port (optional, default to 8080)
        std::uint16_t port = 8080;
        size_t portPos = body.find("\"port\":");
        if (portPos != std::string::npos) {
            portPos += 7;
            size_t portEnd = body.find_first_of(",}", portPos);
            std::string portStr = body.substr(portPos, portEnd - portPos);
            port = static_cast<std::uint16_t>(std::stoi(portStr));
        }
        
        // Call connect callback
        bool success = false;
        if (connectCallback_) {
            success = connectCallback_(ip, port);
        }
        
        if (success) {
            addClient(ip, port);
            res.set_content("{\"success\":true}", "application/json");
        } else {
            res.set_content("{\"success\":false,\"error\":\"Failed to connect\"}", "application/json");
        }
    });

    // API: Get role status
    svr.Get("/api/role", [this](const httplib::Request&, httplib::Response& res) {
        std::ostringstream json;
        json << "{"
             << "\"role\":\"" << currentRole_ << "\","
             << "\"initialized\":" << (initialized_ ? "true" : "false")
             << "}";
        res.set_content(json.str(), "application/json");
    });

    // API: Set role
    svr.Post("/api/role", [this](const httplib::Request& req, httplib::Response& res) {
        std::string body = req.body;
        size_t rolePos = body.find("\"role\":\"");
        if (rolePos != std::string::npos) {
            rolePos += 8;
            size_t endPos = body.find("\"", rolePos);
            if (endPos != std::string::npos) {
                std::string role = body.substr(rolePos, endPos - rolePos);
                currentRole_ = role;
                
                if (roleCallback_) {
                    roleCallback_(role);
                }
                
                res.set_content("{\"success\":true}", "application/json");
                return;
            }
        }
        res.set_content("{\"success\":false,\"error\":\"Invalid request\"}", "application/json");
    });

    // API: Start sync
    svr.Post("/api/start", [this](const httplib::Request& req, httplib::Response& res) {
        std::string body = req.body;
        
        // Parse role
        std::string role;
        size_t rolePos = body.find("\"role\":\"");
        if (rolePos != std::string::npos) {
            rolePos += 8;
            size_t endPos = body.find("\"", rolePos);
            role = body.substr(rolePos, endPos - rolePos);
        }
        
        // Parse folder
        std::string folder;
        size_t folderPos = body.find("\"folder\":\"");
        if (folderPos != std::string::npos) {
            folderPos += 10;
            size_t endPos = body.find("\"", folderPos);
            folder = body.substr(folderPos, endPos - folderPos);
        }
        
        // Parse server IP (for client mode)
        std::string serverIP;
        size_t ipPos = body.find("\"serverIP\":\"");
        if (ipPos != std::string::npos) {
            ipPos += 12;
            size_t endPos = body.find("\"", ipPos);
            serverIP = body.substr(ipPos, endPos - ipPos);
        }
        
        
        // Parse server port (for client mode)
        std::uint16_t serverPort = 8080;
        size_t portPos = body.find("\"serverPort\":");
        if (portPos != std::string::npos) {
            portPos += 13;  // Length of "serverPort": (including the colon)
            size_t portEnd = body.find_first_of(",}", portPos);
            std::string portStr = body.substr(portPos, portEnd - portPos);
            serverPort = static_cast<std::uint16_t>(std::stoi(portStr));
        }
        
        
        // Call start callback
        if (startCallback_) {
            startCallback_(role, folder, serverIP, serverPort);
            initialized_ = true;
            currentRole_ = role;
            
            std::lock_guard<std::mutex> lock(statusMutex_);
            status_.isRunning = true;
            status_.syncFolder = folder;
        }
        
        res.set_content("{\"success\":true}", "application/json");
    });

    // API: Stop sync
    svr.Post("/api/stop", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(statusMutex_);
        status_.isRunning = false;
        res.set_content("{\"success\":true}", "application/json");
    });

    // Start server in background thread
    impl_->serverThread = std::thread([this, &svr]() {
        LOG_INFO("Web server starting on port " + std::to_string(port_));
        running_ = true;
        svr.listen("0.0.0.0", port_);
        running_ = false;
    });

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    return running_;
}

void WebServer::stop() {
    if (!running_) {
        return;
    }
    
    if (impl_->server) {
        impl_->server->stop();
    }
    
    if (impl_->serverThread.joinable()) {
        impl_->serverThread.join();
    }
    
    running_ = false;
}

bool WebServer::isRunning() const {
    return running_;
}

void WebServer::updateStatus(const SyncStatus& status) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = status;
}

SyncStatus WebServer::getStatus() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

void WebServer::addClient(const std::string& ip, std::uint16_t port) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    // Check if client already exists
    for (auto& client : clients_) {
        if (client.ipAddress == ip) {
            client.isConnected = true;
            client.port = port;
            
            // Get current time
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
            client.lastSeen = oss.str();
            return;
        }
    }
    
    // Add new client
    ClientConnection client;
    client.ipAddress = ip;
    client.port = port;
    client.isConnected = true;
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    client.lastSeen = oss.str();
    
    clients_.push_back(client);
}

void WebServer::removeClient(const std::string& ip) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    for (auto& client : clients_) {
        if (client.ipAddress == ip) {
            client.isConnected = false;
            return;
        }
    }
}

std::vector<ClientConnection> WebServer::getClients() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_;
}

void WebServer::addLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(logsMutex_);
    
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%H:%M:%S");
    
    std::string logEntry = "[" + oss.str() + "] " + message;
    logs_.push_back(logEntry);
    
    // Keep only last MAX_LOGS entries
    if (logs_.size() > MAX_LOGS) {
        logs_.erase(logs_.begin());
    }
}

void WebServer::setConfigCallback(std::function<void(const std::string&)> callback) {
    configCallback_ = callback;
}

void WebServer::setConnectCallback(std::function<bool(const std::string&, std::uint16_t)> callback) {
    connectCallback_ = callback;
}

void WebServer::setRoleCallback(std::function<void(const std::string&)> callback) {
    roleCallback_ = callback;
}

void WebServer::setStartCallback(std::function<void(const std::string&, const std::string&, const std::string&, std::uint16_t)> callback) {
    startCallback_ = callback;
}

std::string WebServer::getCurrentRole() const {
    return currentRole_;
}

bool WebServer::isInitialized() const {
    return initialized_;
}

} // namespace sync
