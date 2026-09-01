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
# run_all.ps1 -- register both servers per-user, run every C# harness, unregister.
#
# Registration is `regsvr32 /n /i:user`, i.e. each server's DllInstall with
# AtlSetPerUserRegistration: it writes HKCU\Software\Classes only, needs no
# elevation, touches nothing machine-wide, and is always removed again.
#
# The STAGED copies in out\x64\<Config> are what get registered, so that
# directory is self-contained -- and, more importantly, so there is ONE
# Msgcore.dll on the search path. COM loads an in-proc server with
# LOAD_WITH_ALTERED_SEARCH_PATH, so registering the two servers where they were
# BUILT would have each resolve its own copy of Msgcore.dll: same bytes, two
# modules, two C-runtime states, TWO HEAPS, and a block allocated in one and
# freed in the other. See ComExamples\run_all.ps1, which makes
# the same argument at length.
#
# Every harness reports its verdict the same way all three C++ trees do:
#   0 = SUCCESS   1 = SETUP failure   3 = a check failed / timeout
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
    & (Join-Path $here 'build.ps1') -Config $Config
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null

$msgcoreCom = Join-Path $bin 'MsgcoreCom.dll'
$targetCom  = Join-Path $bin 'TargetCom.dll'
foreach ($dll in @($msgcoreCom, $targetCom)) {
    if (-not (Test-Path $dll)) { throw "missing $dll -- build first" }
}

function Register-Server([string] $dll, [switch] $Unregister) {
    $a = if ($Unregister) { @('/s','/u','/n','/i:user',"`"$dll`"") }
         else             { @('/s','/n','/i:user',"`"$dll`"") }
    $p = Start-Process regsvr32.exe -ArgumentList $a -Wait -PassThru -NoNewWindow
    return $p.ExitCode
}

$rc = Register-Server $msgcoreCom
if ($rc -ne 0) { throw "regsvr32 MsgcoreCom failed with $rc" }
$rc = Register-Server $targetCom
if ($rc -ne 0) { Register-Server $msgcoreCom -Unregister | Out-Null
                 throw "regsvr32 TargetCom failed with $rc" }
Write-Host "registered (per-user) both servers from the staged copies"

$results = [System.Collections.ArrayList]::new()

try {
    # Reading order, which is also dependency order: each builds on the one
    # before, and the two Wsa* harnesses need both servers.
    $harnesses = @(
        'DataFieldTestNet', 'ListVectTestNet', 'MgrPersistTestNet', 'MgrCApiTestNet',
        'WsaStoreTestNet', 'WsaQueryTestNet', 'RecursTimeTestNet', 'BstrWidthTestNet'
    )

    foreach ($name in $harnesses) {
        $exe = Join-Path $bin "$name.exe"
        if (-not (Test-Path $exe)) { throw "missing $exe" }

        # Driven through ProcessStartInfo rather than Start-Process, because
        # `Start-Process -PassThru` WITHOUT -Wait hands back a Process object
        # that never yields an exit code -- $p.ExitCode reads $null even after
        # WaitForExit succeeds, and every harness then scores FAIL however green
        # it was. -Wait would fix the code but lose the timeout. This keeps both.
        #
        # stdout is UTF-8 here (Console.OutputEncoding), unlike the C++ tree's
        # UTF-16, so the reader is told so explicitly; otherwise the € and the
        # rockets in the logs are mojibake. stderr is captured SEPARATELY: a
        # managed unhandled exception goes there, and folding it into the same
        # stream would interleave mid-line with the harness's own output.
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName               = $exe
        $psi.WorkingDirectory       = $bin
        $psi.UseShellExecute        = $false
        $psi.CreateNoWindow         = $true
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError  = $true
        $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
        $psi.StandardErrorEncoding  = [System.Text.Encoding]::UTF8

        $p = [System.Diagnostics.Process]::Start($psi)
        $stdout = $p.StandardOutput.ReadToEnd()
        $stderr = $p.StandardError.ReadToEnd()

        if (-not $p.WaitForExit(120000)) {
            $p.Kill(); $p.WaitForExit()
            Write-Host "$name TIMED OUT"
            $exitCode = 3
        } else {
            $exitCode = $p.ExitCode
        }

        Set-Content -Path (Join-Path $logs "$name.txt") -Value $stdout -Encoding UTF8
        if ($stderr.Trim().Length -gt 0) {
            Set-Content -Path (Join-Path $logs "$name.err.txt") -Value $stderr -Encoding UTF8
        }

        $checks = 0; $failed = 0
        if ($stdout -match '(\d+) checks, (\d+) failed') {
            $checks = [int]$Matches[1]; $failed = [int]$Matches[2]
        }

        [void]$results.Add([pscustomobject]@{
            Harness = $name; Exit = $exitCode; Checks = $checks; Failed = $failed })
    }
}
finally {
    Register-Server $targetCom  -Unregister | Out-Null
    Register-Server $msgcoreCom -Unregister | Out-Null
    Write-Host "unregistered"
}

Write-Host ""
$pass = 0; $fail = 0; $setup = 0; $totalChecks = 0
foreach ($r in $results) {
    switch ($r.Exit) {
        0       { $verdict = 'PASS';  $pass++ }
        1       { $verdict = 'SETUP'; $setup++ }
        default { $verdict = 'FAIL';  $fail++ }
    }
    $totalChecks += $r.Checks
    '{0,-20} exit={1}  {2,5} checks, {3} failed  {4}' -f `
        $r.Harness, $r.Exit, $r.Checks, $r.Failed, $verdict | Write-Host
}

Write-Host ""
Write-Host "$Config : $pass passed, $fail failed, $setup missing-prerequisite ($totalChecks checks)"
Write-Host "logs in $logs"
exit $fail
