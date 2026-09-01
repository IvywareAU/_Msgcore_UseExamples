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
# run_all.ps1 -- build and run every Light harness, and report the exit codes.
#
# Every harness reports its verdict the same way the originals in
# ..\DirectExamples did:
#   0 = SUCCESS   1 = SETUP failure   3 = a check failed / nothing delivered
#
# All eight are single-process: the two mesh harnesses (WsaStoreTest,
# WsaQueryTest) run BOTH hubs inside one process over loopback TCP, so there is
# no server/client pair to start here.
#
#   .\run_all.ps1                    # build + run Debug
#   .\run_all.ps1 -Config Release
#   .\run_all.ps1 -NoBuild

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
    & $msbuild (Join-Path $here 'FacadeExamples(2022).sln') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo -m |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null

$harnesses = @(
    'DataFieldTestLight', 'ListVectTestLight', 'MgrPersistTestLight',
    'MgrCApiTestLight', 'RecursTimeTestLight', 'BstrWidthTestLight',
    'WsaStoreTestLight', 'WsaQueryTestLight'
)

$results = [System.Collections.ArrayList]::new()

foreach ($name in $harnesses) {
    $exe = Join-Path $bin "$name.exe"
    if (-not (Test-Path $exe)) { throw "missing $exe" }
    $log = Join-Path $logs "$name.txt"
    $p = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru -NoNewWindow `
                       -RedirectStandardOutput $log
    $p.WaitForExit(60000) | Out-Null

    # Each harness prints "N checks, M failed." as its last line.
    $tail = (Get-Content $log -Encoding Unicode -ErrorAction SilentlyContinue |
             Where-Object { $_ -match '^\d+ checks' } | Select-Object -Last 1)
    [void]$results.Add([pscustomobject]@{ Harness = $name; Exit = $p.ExitCode; Checks = $tail })
}

# ---- summary -------------------------------------------------------------
Write-Host ""
$pass = 0; $fail = 0; $setup = 0
foreach ($r in $results) {
    switch ($r.Exit) {
        0       { $verdict = 'PASS';  $pass++ }
        1       { $verdict = 'SETUP'; $setup++ }
        default { $verdict = 'FAIL';  $fail++ }
    }
    '{0,-24} exit={1}  {2,-5}  {3}' -f $r.Harness, $r.Exit, $verdict, $r.Checks | Write-Host
}

Write-Host ""
Write-Host "$Config : $pass passed, $fail failed, $setup setup-failure"
Write-Host "logs in $logs"
exit ($fail + $setup)
