#Requires -Version 5.1
<#
.SYNOPSIS
    Registers the "EspScreen Claude Token Refresh" Windows Scheduled Task.

.DESCRIPTION
    Creates (or overwrites) a Scheduled Task that keeps Claude OAuth access
    tokens fresh by invoking refresh-claude-token.ps1 on two triggers:

        1. At user logon — ensures tokens are fresh whenever the session starts.
        2. Every 1 hour (indefinitely) — proactive refresh with the default
           30-minute skew means the access token is renewed before it expires.

    Access tokens issued by the Claude OAuth server are short-lived (~1h).
    Running hourly with a 30-minute threshold ensures the token is refreshed
    well before the device would see an invalid token.

    Manual one-shot equivalent (no Task Scheduler):
        node tools\creds-watcher\refresh.js --force

    To unregister this task later:
        Unregister-ScheduledTask -TaskName "EspScreen Claude Token Refresh"

.NOTES
    Run this script as Administrator (required to register Scheduled Tasks).
    The task runs under the current user account (RunLevel Highest).
#>

# ─── Resolve paths ────────────────────────────────────────────────────────────

$RepoRoot    = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$ScriptPath  = Join-Path $RepoRoot "scripts\refresh-claude-token.ps1"
$TaskName    = "EspScreen Claude Token Refresh"

Write-Host "Registering Scheduled Task: $TaskName"
Write-Host "  Repo root:    $RepoRoot"
Write-Host "  Script path:  $ScriptPath"
Write-Host ""

if (-not (Test-Path $ScriptPath)) {
    Write-Error "Wrapper script not found: $ScriptPath"
    exit 1
}

# ─── Action ───────────────────────────────────────────────────────────────────

# Run PowerShell (bypass execution policy for this specific script only).
$Action = New-ScheduledTaskAction `
    -Execute   "powershell.exe" `
    -Argument  "-NoProfile -ExecutionPolicy Bypass -File `"$ScriptPath`"" `
    -WorkingDirectory $RepoRoot

# ─── Triggers ─────────────────────────────────────────────────────────────────

# Trigger 1: At logon for the current user.
$TriggerLogon = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME

# Trigger 2: Repeating — every 1 hour, starting immediately, no end date.
$TriggerHourly = New-ScheduledTaskTrigger -Once `
    -At (Get-Date) `
    -RepetitionInterval  (New-TimeSpan -Hours 1) `
    -RepetitionDuration  ([TimeSpan]::MaxValue)

# ─── Settings ─────────────────────────────────────────────────────────────────

$Settings = New-ScheduledTaskSettingsSet `
    -ExecutionTimeLimit     (New-TimeSpan -Minutes 5) `
    -RestartCount           2 `
    -RestartInterval        (New-TimeSpan -Minutes 1) `
    -StartWhenAvailable     `
    -MultipleInstances      IgnoreNew

# ─── Register ─────────────────────────────────────────────────────────────────

$Triggers = @($TriggerLogon, $TriggerHourly)

$Task = Register-ScheduledTask `
    -TaskName   $TaskName `
    -Action     $Action `
    -Trigger    $Triggers `
    -Settings   $Settings `
    -RunLevel   Highest `
    -Force

# ─── Summary ──────────────────────────────────────────────────────────────────

if ($Task) {
    Write-Host ""
    Write-Host "Task registered successfully:"
    Write-Host "  Name:      $($Task.TaskName)"
    Write-Host "  State:     $($Task.State)"
    Write-Host "  Triggers:  At logon + every 1 hour (indefinite)"
    Write-Host "  Action:    powershell -ExecutionPolicy Bypass -File `"$ScriptPath`""
    Write-Host "  Timeout:   5 minutes per run"
    Write-Host "  On fail:   restart up to 2 times (1-minute interval)"
    Write-Host ""
    Write-Host "To run immediately:"
    Write-Host "  Start-ScheduledTask -TaskName `"$TaskName`""
    Write-Host ""
    Write-Host "To force a one-shot refresh without Task Scheduler:"
    Write-Host "  node tools\creds-watcher\refresh.js --force"
    Write-Host ""
    Write-Host "To unregister:"
    Write-Host "  Unregister-ScheduledTask -TaskName `"$TaskName`""
} else {
    Write-Error "Task registration failed."
    exit 1
}
