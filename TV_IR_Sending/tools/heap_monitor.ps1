# heap_monitor.ps1
# Polls the TV Remote device's GeneralLog.txt and appends a short summary of
# anything new since the last check to heap_monitor_summary.log. Meant to run
# on a repeating Windows Scheduled Task while we watch for how often the
# heap-fragmentation reboot watchdog (added 2026-08-29) actually fires.

# Board IP: environment variable IR_BOARD_IP (e.g. setx IR_BOARD_IP 192.168.1.50)
$DeviceUrl   = "http://$($env:IR_BOARD_IP)/files/view?f=GeneralLog.txt"
$ToolsDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$StateFile   = Join-Path $ToolsDir "last_general_log.txt"
$SummaryFile = Join-Path $ToolsDir "heap_monitor_summary.log"
$Stamp       = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

try {
    $resp = Invoke-WebRequest -Uri $DeviceUrl -TimeoutSec 8 -UseBasicParsing
    $current = $resp.Content -split "`r?`n"
} catch {
    Add-Content -Path $SummaryFile -Value "$Stamp - device unreachable ($($_.Exception.Message))"
    exit 0
}

$previous = @()
if (Test-Path $StateFile) {
    $previous = Get-Content -Path $StateFile
}

# Find where the previous log's last line still appears in the current log;
# everything after that is new. If it's not found (log got trimmed past it),
# treat the whole current log as new rather than risk missing something.
$newLines = $current
if ($previous.Count -gt 0) {
    $anchor = $previous[-1]
    $idx = [array]::LastIndexOf($current, $anchor)
    if ($idx -ge 0) {
        $newLines = $current[($idx + 1)..($current.Count - 1)]
    }
}

Set-Content -Path $StateFile -Value $current

if ($newLines.Count -eq 0) {
    Add-Content -Path $SummaryFile -Value "$Stamp - checked, nothing new"
    exit 0
}

$fragReboots = $newLines | Where-Object { $_ -match "REBOOT - Heap fragmented" }
$lowHeapReboots = $newLines | Where-Object { $_ -match "REBOOT - Low heap" }
$otherReboots = $newLines | Where-Object { $_ -match "^\S.*REBOOT -" -and $_ -notmatch "Heap fragmented|Low heap" }
$errCount = ($newLines | Where-Object { $_ -match "^\S.*503:" }).Count

$parts = @()
if ($fragReboots.Count -gt 0)    { $parts += "$($fragReboots.Count)x FRAGMENTATION REBOOT (new watchdog fired!)" }
if ($lowHeapReboots.Count -gt 0) { $parts += "$($lowHeapReboots.Count)x low-heap reboot" }
if ($otherReboots.Count -gt 0)   { $parts += "$($otherReboots.Count)x other reboot" }
if ($errCount -gt 0)             { $parts += "$errCount x 503 (low-memory page) events" }

if ($parts.Count -eq 0) {
    Add-Content -Path $SummaryFile -Value "$Stamp - checked, $($newLines.Count) new log line(s), nothing notable"
} else {
    Add-Content -Path $SummaryFile -Value "$Stamp - $($parts -join '; ')"
    foreach ($l in $fragReboots)    { Add-Content -Path $SummaryFile -Value "    $l" }
    foreach ($l in $lowHeapReboots) { Add-Content -Path $SummaryFile -Value "    $l" }
    foreach ($l in $otherReboots)   { Add-Content -Path $SummaryFile -Value "    $l" }
}
