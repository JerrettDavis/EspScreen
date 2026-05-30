#Requires -Version 5.1
<#
.SYNOPSIS
    Wrapper script for the EspScreen Claude OAuth token refresh tool.

.DESCRIPTION
    Runs tools/creds-watcher/refresh.js via Node.js, appends timestamped
    stdout/stderr to a rotating log file, and propagates the Node exit code.

    Intended to be invoked by the Windows Scheduled Task registered via
    register-token-refresh-task.ps1, but can also be run directly:

        # One-shot forced refresh:
        .\scripts\refresh-claude-token.ps1 --force

        # Refresh without pushing to the device:
        .\scripts\refresh-claude-token.ps1 --no-push

        # Pass any other refresh.js flags the same way:
        .\scripts\refresh-claude-token.ps1 --label "Work Account" --threshold-min 15

    Manual one-shot equivalent (no wrapper):
        node tools\creds-watcher\refresh.js --force

.NOTES
    Log file: $env:USERPROFILE\.espscreen\token-refresh.log
    The directory is created automatically if it does not exist.
    Node exit codes are preserved:
        0  Success
        2  Credentials file not found
        3  No refresh token (run `claude login`)
        4  Refresh failed (credentials file untouched)
#>

param()

# ─── Paths ────────────────────────────────────────────────────────────────────

# Resolve creds-watcher directory relative to this script's location.
$ToolDir = Join-Path $PSScriptRoot "..\tools\creds-watcher" | Resolve-Path -ErrorAction SilentlyContinue
if (-not $ToolDir) {
    # Fallback: compute without Resolve-Path (path may not exist on first run)
    $ToolDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\tools\creds-watcher"))
}

$LogDir  = Join-Path $env:USERPROFILE ".espscreen"
$LogFile = Join-Path $LogDir "token-refresh.log"

# ─── Ensure log directory exists ──────────────────────────────────────────────

if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

# ─── Timestamp helper ─────────────────────────────────────────────────────────

function Write-TimestampedLog {
    param([string]$Line)
    $entry = "[$(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssZ')] $Line"
    # Write to log file (append)
    Add-Content -Path $LogFile -Value $entry -Encoding UTF8
    # Also echo to host output
    Write-Host $entry
}

# ─── Run refresh.js ───────────────────────────────────────────────────────────

Write-TimestampedLog "--- EspScreen token refresh starting (args: $args) ---"

# Change working directory to the tool directory
Push-Location $ToolDir

try {
    # Capture stdout+stderr combined, forward all args passed to this script.
    $output = node refresh.js @args 2>&1
    $exitCode = $LASTEXITCODE
} catch {
    Write-TimestampedLog "ERROR: Failed to invoke node: $_"
    Pop-Location
    exit 1
} finally {
    Pop-Location
}

# ─── Log output ───────────────────────────────────────────────────────────────

foreach ($line in $output) {
    Write-TimestampedLog $line
}

Write-TimestampedLog "--- EspScreen token refresh finished (exit code: $exitCode) ---"

# ─── Propagate exit code ──────────────────────────────────────────────────────

exit $exitCode
