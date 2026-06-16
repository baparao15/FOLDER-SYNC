# Unified Folder Sync Application

A real-time, bidirectional folder synchronization tool built in C++17 that keeps a directory in sync across two machines over a TCP network connection. Configured through an embedded web dashboard that auto-opens in your browser.

---

## Features

- **Real-Time Sync** — Automatically detects file creates, updates, and deletes and propagates them instantly.
- **Client-Server Architecture** — One machine acts as the server, others connect as clients. Bidirectional sync keeps both sides up-to-date.
- **Web Dashboard** — A built-in HTTP server hosts a browser UI to select roles, configure sync folders, view live logs, and monitor status.
- **Initial Sync** — When a client connects, the server pushes all existing files so both sides start from the same state.
- **Auto-Reconnect** — The client retries on connection failure; the server waits for reconnection if a client drops.
- **Thread-Safe Queues** — Concurrent producer/consumer queues decouple file watching, network I/O, and file application.
- **Configurable** — All settings (ports, sync interval, log level, etc.) are driven by a single `config.json`.
- **Auto-Launch Browser** — Optionally opens the dashboard in your default browser on startup.

---

## Project Structure

```
os-project/
├── main.cpp                  # Application entry point (role selection, main loops)
├── config.json               # Runtime configuration
├── build.ps1                 # PowerShell build script
├── run_server.ps1            # Start in server mode
├── run_client.ps1            # Start in client mode
│
├── client/                   # Client-specific modules
│   ├── watcher.cpp / .h      #   Directory watcher (snapshot-diff change detection)
│   ├── sender.cpp / .h       #   Network sender (pushes events to server)
│   └── client.cpp            #   Standalone client entry point
│
├── server/                   # Server-specific modules
│   ├── receiver.cpp / .h     #   Network receiver (accepts client connections)
│   ├── web_server.cpp / .h   #   Embedded HTTP server for the dashboard
│   └── server.cpp            #   Standalone server entry point
│
├── shared/                   # Common libraries
│   ├── protocol.cpp / .h     #   Wire protocol (FileEvent, NetworkReader/Writer)
│   ├── file_handler.cpp / .h #   Applies file events to the local filesystem
│   ├── thread_safe_queue.h   #   Template mutex + condition-variable queue
│   ├── config.cpp / .h       #   JSON config loader
│   ├── logger.cpp / .h       #   Thread-safe logger with file output
│   ├── utils.cpp / .h        #   Path helpers and directory utilities
│   ├── browser_launcher.*    #   Opens the default browser on startup
│   └── httplib.h             #   Header-only HTTP library (cpp-httplib)
│
├── web/                      # Dashboard frontend
│   ├── index.html            #   Main HTML page
│   ├── style.css             #   Stylesheet
│   └── app.js                #   Frontend logic (API calls, live updates)
│
└── build/                    # Compiled output (generated)
    └── sync_app.exe
```

---

## OS Concepts Demonstrated

| Concept | Implementation |
|---|---|
| **Multi-threading** | Separate receiver, sender, and watcher threads per role |
| **Synchronization** | `std::mutex` + `std::condition_variable` protecting shared state |
| **IPC / Networking** | Bidirectional communication over raw TCP sockets (Winsock2) |
| **File-system monitoring** | Snapshot-diff strategy using `std::filesystem` |
| **Producer-consumer pattern** | Template `ThreadSafeQueue<T>` decouples detection from transmission |

---

## How It Works

```
┌───────────────┐         TCP (port 8080)         ┌───────────────┐
│    CLIENT     │◄───────────────────────────────►│    SERVER     │
│               │   FileEvents (create/update/    │               │
│  Watcher ──►  │        delete + payload)        │  ◄── Watcher  │
│  Sender  ──►  │                                 │  ◄── Receiver │
│  Handler ◄──  │                                 │  ──► Handler  │
└──────┬────────┘                                 └──────┬────────┘
       │                                                  │
       ▼                                                  ▼
   Local Folder                                      Local Folder
   (kept in sync)                                    (kept in sync)
```

1. **Directory Watcher** monitors the sync folder by diffing filesystem snapshots.
2. Changes are serialized into `FileEvent` objects (operation type + relative path + file bytes).
3. Events are sent over TCP using a simple line-based protocol with binary payloads.
4. The receiving side applies events to its local filesystem via `FileHandler`.
5. A **peer-ID** mechanism prevents echo loops — events originating from a peer are not re-applied locally.

---

## Threading Model

| Thread | Responsibility |
|---|---|
| **Main** | Polls the directory watcher, processes incoming queue, updates web status |
| **Receiver** | Reads events from the network socket into an incoming queue |
| **Sender** | Drains the outgoing queue and transmits events over the network |
| **Web Server** | Serves the dashboard and handles API requests (managed by httplib) |

---

## Prerequisites

| Tool | Requirement |
|---|---|
| **g++** | C++17 support (`-std=c++17`), MinGW-w64 recommended |
| **PowerShell** | 5.1+ (bundled with Windows 10/11) |
| **Windows SDK** | `ws2_32` and `shell32` (included with MinGW / MSYS2) |

---

## Build

```powershell
.\build.ps1
```

Compiles all source files and produces `build\sync_app.exe`.

---

## Run

```powershell
.\build\sync_app.exe          # default web UI port 8888
.\build\sync_app.exe 9000     # custom web UI port
```

The application will:
1. Start an embedded web server.
2. Auto-open your browser to `http://localhost:8888`.
3. Present a dashboard to choose **Server** or **Client** mode.

---

## Usage

### Two machines on the same network

1. Run `sync_app.exe` on **Machine A** → select **Server** → choose a folder → click **Start Sync**.
2. Run `sync_app.exe` on **Machine B** → select **Client** → enter Machine A's IP and port → choose a folder → click **Start Sync**.

### Single machine test

Run two instances (pass a different web port to the second: `.\build\sync_app.exe 9000`). Set one as Server and one as Client pointing to `localhost`.

---

## Configuration (`config.json`)

```json
{
    "sync_folder":            "C:\\Users\\YourName\\Desktop\\sync_folder",
    "sync_interval_seconds":  3,
    "log_level":              "INFO",
    "log_to_file":            false,
    "log_file_path":          "sync.log",
    "server_host":            "localhost",
    "server_port":            8080,
    "web_server_port":        8888,
    "auto_launch_browser":    true
}
```

| Key | Default | Description |
|---|---|---|
| `sync_folder` | *(set in UI)* | Absolute path of the folder to watch |
| `sync_interval_seconds` | `3` | Polling interval for change detection (seconds) |
| `log_level` | `INFO` | `DEBUG`, `INFO`, `WARNING`, or `ERROR` |
| `log_to_file` | `false` | Write logs to a file in addition to console |
| `log_file_path` | `sync.log` | Log file location |
| `server_host` | `localhost` | Default server address for client connections |
| `server_port` | `8080` | TCP port for file-sync traffic |
| `web_server_port` | `8888` | Port for the web dashboard |
| `auto_launch_browser` | `true` | Open browser automatically on startup |

---

## File Events

| Type | Trigger |
|---|---|
| `Create` | New file or directory appears in the watched folder |
| `Update` | File modification time changes |
| `Delete` | File or directory is removed |

---

## Limitations

- One client per server session (no multi-client fan-out)
- Polling-based change detection (not inotify / FSEvents)
- Windows only (Winsock2 + ShellExecute for browser launch)

---


*Developed as an Operating Systems course project.*
