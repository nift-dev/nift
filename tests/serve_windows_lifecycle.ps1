# `nift serve` Windows lifecycle test (process lifetime, not just compilation).
#
#   pwsh -File tests/serve_windows_lifecycle.ps1 -Nift .\nift.exe
#
# 1. Start `nift serve` on a free port, fetch a file and compare bytes, then
#    force-kill nift (Stop-Process -Force = TerminateProcess, like
#    taskkill /F). Every Python process under nift (including the real
#    python.exe below a py.exe launcher) must be gone and the port free within
#    5 s. This relies on the kill-on-close job; nift must not print its "could
#    not arrange ... force-killed" warning on a CI runner.
# 2. Ctrl+Break: nift is started in its own process group (CREATE_NEW_PROCESS_GROUP
#    through a Python helper), CTRL_BREAK_EVENT is sent to that group, and nift
#    must exit 130 with no Python left and the port free. Ctrl+C cannot be
#    tested the same way: Windows disables CTRL_C_EVENT for a new process group,
#    and sending it to the runner's shared console group would interrupt the CI
#    shell itself. Ctrl+C reaches Python through the same console mechanism and
#    Python handles it as KeyboardInterrupt (exit 0).
# Every wait is bounded; anything started is killed in `finally`.
param([Parameter(Mandatory = $true)][string]$Nift)
$ErrorActionPreference = 'Stop'
$Nift = (Resolve-Path $Nift).Path
$failures = 0
function Ok($m) { Write-Host "ok   - $m" }
function Bad($m) { Write-Host "FAIL - $m"; $script:failures++ }

function Get-FreePort {
    $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $l.Start(); $p = $l.LocalEndpoint.Port; $l.Stop(); return $p
}
function Test-Listening([int]$port) {
    $c = [System.Net.Sockets.TcpClient]::new()
    try { $c.Connect('127.0.0.1', $port); return $true } catch { return $false } finally { $c.Dispose() }
}
function Get-Descendants([int]$root) {
    $all = @(Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name)
    $found = @(); $frontier = @($root)
    while ($frontier.Count -gt 0) {
        $next = @()
        foreach ($p in $all) { if ($frontier -contains [int]$p.ParentProcessId) { $found += $p; $next += [int]$p.ProcessId } }
        $frontier = $next
    }
    return $found
}
function Wait-Fetch([string]$url, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        try { return [System.Net.WebClient]::new().DownloadData($url) } catch { Start-Sleep -Milliseconds 200 }
    }
    return $null
}
function Wait-Gone([int[]]$pids, [int]$port, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $alive = @($pids | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
        if ($alive.Count -eq 0 -and -not (Test-Listening $port)) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("nift-serve-win-" + [guid]::NewGuid())
$www = Join-Path $work 'www dir'
New-Item -ItemType Directory -Force -Path $www | Out-Null
$bytes = [byte[]]::new(4096); [System.Random]::new(7).NextBytes($bytes)
[System.IO.File]::WriteAllBytes((Join-Path $www 'blob.bin'), $bytes)
$started = @()
try {
    # --- 1. force-kill ---------------------------------------------------
    $port = Get-FreePort
    $out = Join-Path $work 'kill.out'; $err = Join-Path $work 'kill.err'
    $proc = Start-Process -FilePath $Nift -ArgumentList @('serve', '--dir', "`"$www`"", '--port', $port) `
        -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    $started += $proc.Id
    $got = Wait-Fetch "http://127.0.0.1:$port/blob.bin" 20
    if ($null -ne $got -and [System.Linq.Enumerable]::SequenceEqual([byte[]]$got, $bytes)) { Ok 'fetched file matches bytes' }
    else { Bad 'fetch failed'; Get-Content $out, $err -ErrorAction SilentlyContinue | Write-Host }
    $children = @(Get-Descendants $proc.Id)
    $childPids = @($children | ForEach-Object { [int]$_.ProcessId })
    $started += $childPids
    if (@($children | Where-Object { $_.Name -match '^(python|py)' }).Count -gt 0) { Ok "python child running ($($children.Name -join ', '))" }
    else { Bad "no python child found under nift ($($children.Name -join ', '))" }
    if (Select-String -Path $err -Pattern 'could not arrange' -Quiet) { Bad 'nift warned that the kill-on-close job could not be established' }
    else { Ok 'kill-on-close job established (no warning)' }
    Stop-Process -Id $proc.Id -Force
    if (Wait-Gone ($childPids + $proc.Id) $port 5) { Ok 'force-kill: no python left, port free within 5s' }
    else { Bad 'force-kill: python survived or port still listening' }

    # --- 2. Ctrl+Break -------------------------------------------------------
    $port = Get-FreePort
    $helper = @'
import os, signal, subprocess, sys, time, urllib.request
nift, www, port = sys.argv[1], sys.argv[2], sys.argv[3]
p = subprocess.Popen([nift, "serve", "--dir", www, "--port", port],
                     creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
print("PID", p.pid, flush=True)
deadline = time.time() + 20
while time.time() < deadline:
    try:
        urllib.request.urlopen(f"http://127.0.0.1:{port}/blob.bin", timeout=2).read(); break
    except Exception:
        time.sleep(0.2)
else:
    p.kill(); print("SERVER DID NOT START", flush=True); sys.exit(2)
os.kill(p.pid, signal.CTRL_BREAK_EVENT)
try:
    print("EXIT", p.wait(timeout=10), flush=True)
except subprocess.TimeoutExpired:
    p.kill(); print("NO EXIT AFTER CTRL_BREAK", flush=True); sys.exit(3)
'@
    $helperPath = Join-Path $work 'ctrl_break.py'
    Set-Content -Path $helperPath -Value $helper -Encoding UTF8
    $python = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $python) { Bad 'python not on PATH for the Ctrl+Break helper' }
    else {
        $lines = & $python -I $helperPath $Nift $www $port 2>&1 | ForEach-Object { "$_" }
        $lines | Write-Host
        $exitLine = $lines | Where-Object { $_ -match '^EXIT ' } | Select-Object -First 1
        if ($exitLine -eq 'EXIT 130') { Ok 'Ctrl+Break: nift exits 130' } else { Bad "Ctrl+Break: expected EXIT 130, got '$exitLine'" }
        $pidLine = $lines | Where-Object { $_ -match '^PID ' } | Select-Object -First 1
        if ($pidLine) { $started += [int]($pidLine -split ' ')[1] }
        if (Wait-Gone @() $port 5) { Ok 'Ctrl+Break: port free' } else { Bad 'Ctrl+Break: port still listening' }
        $left = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match "http\.server $port" })
        if ($left.Count -eq 0) { Ok 'Ctrl+Break: no python left' } else { Bad 'Ctrl+Break: python left running'; $started += @($left | ForEach-Object { [int]$_.ProcessId }) }
    }
}
finally {
    foreach ($id in $started) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
if ($failures -gt 0) { Write-Host "serve_windows_lifecycle: $failures failed"; exit 1 }
Write-Host 'serve_windows_lifecycle: all passed'
exit 0
