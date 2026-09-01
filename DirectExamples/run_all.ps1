# Copyright © 2026 Khrustal & Mann
#              MELBOURNE, VICTORIA, AUSTRALIA, 3000
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied. See the License for the specific language governing
# permissions and limitations under the License.
#
# run_all.ps1 -- build and run all eight harnesses, and report the exit codes.
#
# Every harness reports its verdict the same way:
#   0 = SUCCESS   1 = SETUP failure   2 = MFC/CRT assertion   3 = a check failed
#
#   .\run_all.ps1                    # build + run Debug
#   .\run_all.ps1 -Config Release
#   .\run_all.ps1 -NoBuild
#
# All eight are single-process and headless -- there is no interactive harness in
# this tree and no hardware prerequisite. WsaStoreTest and WsaQueryTest do open
# loopback TCP sockets (two hubs in one process, P2PeerConWsa on 127.0.0.1), so a
# host firewall that blocks 127.0.0.1 will show up as their timing out at exit 3
# rather than as a SETUP failure.
#
# They are listed here in the reading order the README gives, which is also the
# order in which they build on each other -- if several fail at once, the first
# failure in this list is the one to look at.

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug',
    [switch] $NoBuild
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin  = Join-Path $here "out\x64\$Config"
$logs = Join-Path $here "logs\$Config"

if (-not $NoBuild) {
    $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
                 -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe |
               Select-Object -First 1
    if (-not $msbuild) { throw "MSBuild not found" }

    Write-Host "building $Config..."
    & $msbuild (Join-Path $here 'DirectExamples(2022).sln') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo -m |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null

$harnesses = @(
    'DataFieldTest', 'ListVectTest', 'MgrPersistTest', 'MgrCApiTest',
    'WsaStoreTest', 'WsaQueryTest', 'RecursTimeTest', 'BstrWidthTest'
)

$results = [System.Collections.ArrayList]::new()

# Wait for a process, and KILL it if it overruns. Without the kill, a harness
# that hangs leaves $p.ExitCode unreadable (it throws on a live process) and the
# script dies with an exception instead of reporting a timeout -- which is the
# one verdict a harness runner most needs to be able to report. 3 is the tree's
# own failure code, so a killed run reads the same as a self-detected failure.
function Wait-Harness([System.Diagnostics.Process] $p, [int] $ms = 60000) {
    if ($p.WaitForExit($ms)) { return $p.ExitCode }
    Write-Warning ("{0} did not exit within {1}s -- killing it." -f $p.ProcessName, ($ms / 1000))
    try { $p.Kill($true) } catch { }
    $p.WaitForExit(5000) | Out-Null
    return 3
}

foreach ($name in $harnesses) {
    $exe = Join-Path $bin "$name.exe"
    if (-not (Test-Path $exe)) { throw "missing $exe" }
    $log = Join-Path $logs "$name.txt"
    $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru -NoNewWindow `
                       -RedirectStandardOutput $log
    [void]$results.Add([pscustomobject]@{ Harness = $name; Exit = (Wait-Harness $p) })
}

# ---- summary -------------------------------------------------------------
Write-Host ""
$pass = 0; $fail = 0; $setup = 0
foreach ($r in $results) {
    switch ($r.Exit) {
        0       { $verdict = 'PASS';    $pass++ }
        1       { $verdict = 'SETUP';   $setup++ }
        default { $verdict = 'FAIL';    $fail++ }
    }
    '{0,-20} exit={1}  {2}' -f $r.Harness, $r.Exit, $verdict | Write-Host
}

Write-Host ""
Write-Host "$Config : $pass passed, $fail failed, $setup missing-prerequisite"
Write-Host "logs in $logs"
exit $fail
