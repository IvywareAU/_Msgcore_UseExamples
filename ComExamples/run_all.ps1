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
# run_all.ps1 -- stage both COM servers, register them per-user, run the eight
#                harnesses, unregister.
#
# Registration is `regsvr32 /n /i:user`, i.e. each server's DllInstall with
# AtlSetPerUserRegistration -- it writes HKCU\Software\Classes only, needs no
# elevation, touches nothing machine-wide, and is always removed again.
#
# ------------------------------------------------------------------------------
# WHY EVERYTHING IS STAGED INTO ONE DIRECTORY, and it is not tidiness.
#
# The two networked harnesses load BOTH servers into one process, and both
# servers depend on Msgcore.dll:
#
#       MsgcoreCom.dll  ->  MsgFacade.dll     ->  Msgcore.dll
#       TargetCom.dll   ->  TargetFacade.dll  ->  TargetCore.dll  ->  Msgcore.dll
#
# MsgFacade.dll is the newer of the two chains: MsgcoreCom used to call Msgcore
# directly through its flat C ABI and so had one fewer link.
#
# COM loads an in-proc server with LOAD_WITH_ALTERED_SEARCH_PATH, so each server
# resolves its dependencies from ITS OWN directory first. Register the two where
# they were built -- MsgFacade\com\out and TargetFacade\out -- and the loader
# finds a Msgcore.dll in each, and loads BOTH: same bytes, two modules, two
# C-runtime states, TWO HEAPS. A block allocated inside one and freed inside the
# other then trips `_CrtIsValidHeapPointer` in the debug CRT, and silently
# corrupts the heap in a release build.
#
# Staging both servers and all four core DLLs into one directory makes that
# impossible: there is one Msgcore.dll on the search path and therefore one
# module. It is also what makes this directory self-contained, so nothing here
# depends on the MsgFacade\ or TargetFacade\ trees being registered.
# ------------------------------------------------------------------------------
#
# Every harness reports its verdict the same way the other trees do:
#   0 = SUCCESS   1 = SETUP failure   2 = ASSERT   3 = a check failed / timeout
#
#   .\run_all.ps1                    # build + run Debug
#   .\run_all.ps1 -Config Release
#   .\run_all.ps1 -NoBuild
#   .\run_all.ps1 -IncludeScripts    # also run the late-bound PowerShell client

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug',
    [switch] $NoBuild,
    [switch] $IncludeScripts
)

$ErrorActionPreference = 'Stop'
$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$mscs  = Resolve-Path (Join-Path $here '..\..')   # ComExamples\ -> repo root -> MSCS
$bin   = Join-Path $here "out\x64\$Config"
$logs  = Join-Path $here "logs\$Config"

# Msgcore.dll comes from ITS OWN BUILD OUTPUT, not from the shared bin\<Config>64
# folder. Nothing deploys Msgcore there automatically -- it is refreshed by hand
# -- so staging from it means a rebuilt core silently does not reach these
# harnesses, and the failure is not a missing symbol at build time but
# `regsvr32 ... failed with 3` at registration, because MsgcoreCom imports an
# entry point the stale copy never exported. Staging the build output makes this
# tree depend on what was actually just compiled.
#
# TargetCore and TargetFacade still come from the shared folder: this tree does
# not build them, and they are reached only through TargetCom.
$coreBin = if ($Config -eq 'Debug') { Join-Path $mscs 'bin\Debug64' } else { Join-Path $mscs 'bin\Release64' }
$msgcoreBin    = Join-Path $mscs "Msgcore\out\x64\$Config"
$msgFacadeBin  = Join-Path $mscs "MsgFacade\out\x64\$Config"
$msgcoreComDir = Join-Path $mscs "MsgFacade\com\out\x64\$Config"
$p2pComDir     = Join-Path $mscs "TargetFacade\out\x64\$Config"

function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found" }
    $p = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe |
         Select-Object -First 1
    if (-not $p) { throw "MSBuild not found" }
    return $p
}

if (-not $NoBuild) {
    $msbuild = Find-MSBuild

    # MsgFacade before MsgcoreCom: the server links its import library and its
    # post-build step stages the DLL, so building it out of order fails with a
    # sentence about a missing file rather than a compiler error.
    Write-Host "building MsgFacade ($Config)..."
    & $msbuild (Join-Path $mscs 'MsgFacade\MsgFacade(2022).vcxproj') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "MsgFacade build failed" }

    # The two servers next: these harnesses compile against their MIDL output
    # and load them at run time.
    Write-Host "building MsgcoreCom ($Config)..."
    & $msbuild (Join-Path $mscs 'MsgFacade\com\MsgcoreCom(2022).vcxproj') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "MsgcoreCom build failed" }

    Write-Host "building TargetCom ($Config)..."
    & $msbuild (Join-Path $mscs 'TargetFacade\com\TargetCom(2022).vcxproj') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "TargetCom build failed" }

    Write-Host "building harnesses ($Config)..."
    & $msbuild (Join-Path $here 'ComExamples(2022).sln') `
        -p:Configuration=$Config -p:Platform=x64 -v:minimal -nologo -m |
        Where-Object { $_ -match 'error|warning C' }
    if ($LASTEXITCODE -ne 0) { throw "harness build failed" }
}

New-Item -ItemType Directory -Force -Path $logs | Out-Null
New-Item -ItemType Directory -Force -Path $bin  | Out-Null

# ---- stage: ONE directory, ONE Msgcore.dll (see the header) -----------------
$stage = @(
    (Join-Path $msgcoreComDir 'MsgcoreCom.dll'),
    (Join-Path $msgFacadeBin  'MsgFacade.dll'),
    (Join-Path $p2pComDir     'TargetCom.dll'),
    (Join-Path $p2pComDir     'TargetFacade.dll'),
    (Join-Path $msgcoreBin    'Msgcore.dll'),
    (Join-Path $coreBin       'TargetCore.dll')
)
foreach ($f in $stage) {
    if (-not (Test-Path $f)) { throw "missing $f -- build it first" }
    Copy-Item $f -Destination $bin -Force
}
Write-Host "staged $($stage.Count) DLLs into $bin"

$msgcoreCom = Join-Path $bin 'MsgcoreCom.dll'
$targetCom  = Join-Path $bin 'TargetCom.dll'

function Register-Server([string] $dll, [switch] $Unregister) {
    $args = if ($Unregister) { @('/s','/u','/n','/i:user',"`"$dll`"") }
            else             { @('/s','/n','/i:user',"`"$dll`"") }
    $p = Start-Process regsvr32.exe -ArgumentList $args -Wait -PassThru -NoNewWindow
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
    # In reading order, which is also dependency order: each builds on the one
    # before, and the last two need both servers.
    $harnesses = @(
        'DataFieldTestCom', 'ListVectTestCom', 'MgrPersistTestCom', 'MgrCApiTestCom',
        'WsaStoreTestCom', 'WsaQueryTestCom', 'RecursTimeTestCom', 'BstrWidthTestCom'
    )

    foreach ($name in $harnesses) {
        $exe = Join-Path $bin "$name.exe"
        if (-not (Test-Path $exe)) { throw "missing $exe" }

        $out = Join-Path $logs "$name.txt"

        # Driven through ProcessStartInfo rather than Start-Process, because
        # `Start-Process -PassThru` WITHOUT -Wait hands back a Process object
        # that never yields an exit code: $p.ExitCode reads $null even after
        # WaitForExit succeeds, and every harness then scores FAIL however green
        # it was. -Wait would fix the code but lose the timeout. This keeps both.
        #
        # The harnesses print UTF-16 (_setmode _O_U16TEXT), so the reader is
        # told so explicitly; otherwise the log is mojibake and the tally regex
        # never matches.
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName               = $exe
        $psi.WorkingDirectory       = $bin
        $psi.UseShellExecute        = $false
        $psi.CreateNoWindow         = $true
        $psi.RedirectStandardOutput = $true
        $psi.StandardOutputEncoding = [System.Text.Encoding]::Unicode

        $p = [System.Diagnostics.Process]::Start($psi)
        # Read the stream while the process runs: a harness that outproduces the
        # pipe buffer would otherwise block forever and be scored a timeout.
        $stdout = $p.StandardOutput.ReadToEnd()
        if (-not $p.WaitForExit(60000)) { $p.Kill(); $p.WaitForExit(); Write-Host "$name TIMED OUT" }
        $exitCode = $p.ExitCode
        Set-Content -LiteralPath $out -Value $stdout -Encoding Unicode

        $tally = ($stdout -split "`r?`n" |
                  Select-String -Pattern '^\d+ checks' | Select-Object -Last 1)
        [void]$results.Add([pscustomobject]@{
            Harness = $name; Exit = $exitCode; Tally = "$tally"
        })
    }

    if ($IncludeScripts) {
        # The late-bound client: no compiler, no header, no import lib, no
        # interop assembly. It is scored like the rest -- its exit code is the
        # number of failed checks -- and it is off by default because it needs
        # a second PowerShell, which is slow, not because it is optional.
        $ps1   = Join-Path $here 'script\ps_client.ps1'
        $psLog = Join-Path $logs 'ps_client.txt'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ps1 -Config $Config 2>&1 |
            Out-File -FilePath $psLog -Encoding utf8
        $psExit = $LASTEXITCODE
        [void]$results.Add([pscustomobject]@{
            Harness = 'script\ps_client.ps1'; Exit = $psExit; Tally = "$psExit failed"
        })
    }
}
finally {
    Register-Server $targetCom  -Unregister | Out-Null
    Register-Server $msgcoreCom -Unregister | Out-Null
    Write-Host "unregistered"
}

Write-Host ""
$pass = 0; $fail = 0; $setup = 0
foreach ($r in $results) {
    switch ($r.Exit) {
        0       { $verdict = 'PASS';   $pass++ }
        1       { $verdict = 'SETUP';  $setup++ }
        2       { $verdict = 'ASSERT'; $fail++ }
        default { $verdict = 'FAIL';   $fail++ }
    }
    '{0,-20} exit={1,-3} {2,-7} {3}' -f $r.Harness, $r.Exit, $verdict, $r.Tally | Write-Host
}

Write-Host ""
Write-Host "$Config : $pass passed, $fail failed, $setup missing-prerequisite"
Write-Host "logs in $logs"
exit $fail
