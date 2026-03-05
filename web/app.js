// API Base URL - dynamically use the current page's port
const API_BASE = `http://${window.location.hostname}:${window.location.port}/api`;

// State
let updateInterval = null;
let currentRole = 'unset';
let isInitialized = false;

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    console.log('Folder Sync Web Interface initialized');

    // Set up role selection listeners
    document.getElementById('roleServer').addEventListener('change', handleRoleChange);
    document.getElementById('roleClient').addEventListener('change', handleRoleChange);

    // Always show role selection screen on startup
    // User must choose their role each time they start the application

    // Start periodic updates - refresh statistics every second to catch changes quickly
    updateInterval = setInterval(() => {
        if (isInitialized) {
            updateStatus();
            updateLogs();
        }
    }, 1000);
});

// Handle role selection change
function handleRoleChange() {
    const serverRadio = document.getElementById('roleServer');
    const clientRadio = document.getElementById('roleClient');
    const clientFields = document.getElementById('clientFields');
    const startBtn = document.getElementById('startBtn');

    if (clientRadio.checked) {
        clientFields.style.display = 'block';
    } else {
        clientFields.style.display = 'none';
    }

    // Enable start button if a role is selected
    startBtn.disabled = !(serverRadio.checked || clientRadio.checked);
}

// Start sync with selected configuration
async function startSync() {
    const serverRadio = document.getElementById('roleServer');
    const clientRadio = document.getElementById('roleClient');
    const folder = document.getElementById('initialSyncFolder').value.trim();

    if (!folder) {
        alert('Please enter a sync folder path');
        return;
    }

    const role = serverRadio.checked ? 'server' : 'client';

    const payload = {
        role: role,
        folder: folder
    };

    // Add server details for client mode
    if (role === 'client') {
        const serverIP = document.getElementById('serverIP').value.trim();
        const serverPort = parseInt(document.getElementById('serverPort').value) || 8080;

        if (!serverIP) {
            alert('Please enter the server IP address');
            return;
        }

        payload.serverIP = serverIP;
        payload.serverPort = serverPort;
    }

    try {
        const response = await fetch(`${API_BASE}/start`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(payload)
        });

        const data = await response.json();

        if (data.success) {
            currentRole = role;
            isInitialized = true;
            showMainInterface();
            showNotification(`${role.charAt(0).toUpperCase() + role.slice(1)} mode started successfully`, 'success');
        } else {
            showNotification('Failed to start sync: ' + (data.error || 'Unknown error'), 'error');
        }
    } catch (error) {
        console.error('Error starting sync:', error);
        showNotification('Failed to start sync', 'error');
    }
}

// Show main interface and hide role selection
function showMainInterface() {
    document.getElementById('roleSelection').style.display = 'none';
    document.getElementById('mainInterface').style.display = 'block';
    document.getElementById('currentMode').textContent = currentRole.charAt(0).toUpperCase() + currentRole.slice(1);
}

// Update server status
async function updateStatus() {
    try {
        const response = await fetch(`${API_BASE}/status`);
        if (!response.ok) {
            throw new Error('Failed to fetch status');
        }

        const data = await response.json();

        // Update status badge
        const statusBadge = document.getElementById('statusBadge');
        const statusText = document.getElementById('statusText');

        if (data.isRunning) {
            statusBadge.classList.remove('inactive');
            statusText.textContent = 'Running';
        } else {
            statusBadge.classList.add('inactive');
            statusText.textContent = 'Stopped';
        }

        // Update sync folder
        if (data.syncFolder) {
            document.getElementById('syncFolder').value = data.syncFolder;
        }

        // Update statistics
        document.getElementById('filesSynced').textContent = data.totalFilesSynced || 0;
        document.getElementById('bytesSynced').textContent = formatBytes(data.totalBytesSynced || 0);
        document.getElementById('connectedCount').textContent = data.connectedClients || 0;
        document.getElementById('lastSync').textContent = data.lastSyncTime || 'Never';

    } catch (error) {
        console.error('Error updating status:', error);
        const statusBadge = document.getElementById('statusBadge');
        const statusText = document.getElementById('statusText');
        statusBadge.classList.add('inactive');
        statusText.textContent = 'Disconnected';
    }
}

// Update activity logs
async function updateLogs() {
    try {
        const response = await fetch(`${API_BASE}/logs`);
        if (!response.ok) {
            return;
        }

        const data = await response.json();
        const container = document.getElementById('logContainer');

        if (data.logs && data.logs.length > 0) {
            // Only update if logs have changed
            const currentLogs = container.innerHTML;
            const newLogs = data.logs.map(log =>
                `<div class="log-entry">${escapeHtml(log)}</div>`
            ).join('');

            if (currentLogs !== newLogs) {
                container.innerHTML = newLogs;
                // Scroll to bottom
                container.scrollTop = container.scrollHeight;
            }
        }
    } catch (error) {
        console.error('Error updating logs:', error);
    }
}

// Update configuration
async function updateConfig() {
    const folder = document.getElementById('syncFolder').value.trim();

    if (!folder) {
        alert('Please enter a folder path');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/config`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ folder })
        });

        const data = await response.json();

        if (data.success) {
            showNotification('Configuration updated successfully', 'success');
            // Immediately refresh statistics after config change
            await updateStatus();
        } else {
            showNotification('Failed to update configuration: ' + (data.error || 'Unknown error'), 'error');
        }
    } catch (error) {
        console.error('Error updating config:', error);
        showNotification('Failed to update configuration', 'error');
    }
}

// Utility: Format bytes
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';

    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));

    return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i];
}

// Utility: Escape HTML
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// Utility: Show notification
function showNotification(message, type = 'info') {
    console.log(`[${type.toUpperCase()}] ${message}`);

    // Add to activity log
    const container = document.getElementById('logContainer');
    const entry = document.createElement('div');
    entry.className = 'log-entry';
    entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
    container.appendChild(entry);
    container.scrollTop = container.scrollHeight;
}

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
    if (updateInterval) {
        clearInterval(updateInterval);
    }
});
