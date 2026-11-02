param(
    [Parameter(Mandatory = $true)]
    [int]$RootPid
)

$ErrorActionPreference = 'Stop'

$processes = Get-CimInstance Win32_Process
$ids = [System.Collections.Generic.HashSet[int]]::new()
[void]$ids.Add($RootPid)
$changed = $true
while ($changed) {
    $changed = $false
    foreach ($process in $processes) {
        if ($ids.Contains([int]$process.ParentProcessId) -and -not $ids.Contains([int]$process.ProcessId)) {
            [void]$ids.Add([int]$process.ProcessId)
            $changed = $true
        }
    }
}

$live = Get-Process -Id @($ids) -ErrorAction SilentlyContinue
$serverCount = @($processes | Where-Object {
    $_.Name -eq 'klogg_server.exe' -and $ids.Contains([int]$_.ProcessId)
}).Count
[pscustomobject]@{
    processes = @($live).Count
    workingSetMb = [math]::Round((($live | Measure-Object WorkingSet64 -Sum).Sum / 1MB), 2)
    privateMb = [math]::Round((($live | Measure-Object PrivateMemorySize64 -Sum).Sum / 1MB), 2)
    serverProcesses = $serverCount
} | ConvertTo-Json -Compress
