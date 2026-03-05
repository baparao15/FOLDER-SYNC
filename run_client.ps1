# Run Client Instance
# This script runs the sync application as a CLIENT on port 8889

Write-Host "=== Starting Sync Application - CLIENT MODE ===" -ForegroundColor Green
Write-Host "Web Interface: http://localhost:8889" -ForegroundColor Cyan
Write-Host ""

.\build\sync_app.exe 8889
