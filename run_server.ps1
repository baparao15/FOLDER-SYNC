# Run Server Instance
# This script runs the sync application as a SERVER on port 8888

Write-Host "=== Starting Sync Application - SERVER MODE ===" -ForegroundColor Green
Write-Host "Web Interface: http://localhost:8888" -ForegroundColor Cyan
Write-Host ""

.\build\sync_app.exe 8888
