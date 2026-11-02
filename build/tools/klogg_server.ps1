param(
    [ValidateSet('install', 'start', 'stop', 'restart', 'status')]
    [string]$Action = 'status'
)

$ErrorActionPreference = 'Stop'
$taskName = 'KloggWebServer'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\output\klogg_server.exe'
$webRoot = Join-Path $root 'src\web\dist'
$healthUrl = 'http://127.0.0.1:8080/health'
$currentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

function Stop-ServerProcess {
    Get-Process klogg_server -ErrorAction SilentlyContinue | Stop-Process -Force
}

function Wait-Health([int]$seconds = 15) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $result = Invoke-RestMethod -Uri $healthUrl -TimeoutSec 2
            if ($result.ok) {
                return $true
            }
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    return $false
}

function Install-KloggTask {
    if (-not (Test-Path $exe)) {
        throw "Server executable not found: $exe"
    }

    $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($task) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    }
    Stop-ServerProcess

    $arguments = "--port 8080 --listen 127.0.0.1 --web-root `"$webRoot`""
    $action = New-ScheduledTaskAction -Execute $exe -Argument $arguments -WorkingDirectory $root
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $currentUser
    $settings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries `
        -StartWhenAvailable `
        -MultipleInstances IgnoreNew `
        -RestartCount 999 `
        -RestartInterval (New-TimeSpan -Minutes 1) `
        -ExecutionTimeLimit ([TimeSpan]::Zero)

    Register-ScheduledTask `
        -TaskName $taskName `
        -Action $action `
        -Trigger $trigger `
        -Settings $settings `
        -Description 'klogg Web UI server on http://127.0.0.1:8080' `
        -User $currentUser `
        -Force | Out-Null

    Start-ScheduledTask -TaskName $taskName
    if (Wait-Health) {
        Write-Host "KloggWebServer installed and running on $healthUrl"
    }
    else {
        throw 'Task installed but the server did not become healthy.'
    }
}

switch ($Action) {
    'install' {
        Install-KloggTask
    }
    'start' {
        $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if (-not $task) {
            Install-KloggTask
        }
        else {
            Start-ScheduledTask -TaskName $taskName
            if (-not (Wait-Health)) {
                throw 'Server did not become healthy.'
            }
            Write-Host "KloggWebServer running on $healthUrl"
        }
    }
    'stop' {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Stop-ServerProcess
        Write-Host 'KloggWebServer stopped'
    }
    'restart' {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Stop-ServerProcess
        Start-ScheduledTask -TaskName $taskName
        if (-not (Wait-Health)) {
            throw 'Server did not become healthy.'
        }
        Write-Host "KloggWebServer restarted on $healthUrl"
    }
    'status' {
        $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if ($task) {
            $info = Get-ScheduledTaskInfo -TaskName $taskName
            Write-Host "Task:       $($task.State)"
            Write-Host "Last run:   $($info.LastRunTime)"
            Write-Host "Last result:$($info.LastTaskResult)"
        }
        else {
            Write-Host 'Task:       not installed'
        }
        $process = Get-Process klogg_server -ErrorAction SilentlyContinue
        if ($process) {
            Write-Host "Process:    PID $($process.Id) started $($process.StartTime)"
        }
        else {
            Write-Host 'Process:    not running'
        }
        try {
            $result = Invoke-RestMethod -Uri $healthUrl -TimeoutSec 2
            Write-Host "Health:     $($result | ConvertTo-Json -Compress)"
        } catch {
            Write-Host 'Health:     failed'
        }
    }
}
