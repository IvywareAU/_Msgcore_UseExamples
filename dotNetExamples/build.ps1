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
# build.ps1 -- compile the eight C# harnesses and stage the runtime DLLs.
#
# WHY csc AND NOT A .csproj. This tree needs no NuGet package, no SDK, no
# targeting pack and no project system: every harness is `common\*.cs` plus one
# file, referencing nothing but mscorlib/System/System.Core. Driving Roslyn
# directly makes that visible and keeps the tree buildable on a machine with the
# C++ workload only -- which is what MSCS boxes usually have. An SDK-style
# .csproj with <TargetFramework>net48</> and these same sources builds
# identically if you would rather have VS integration.
#
# TARGET: .NET Framework 4.8, x64. x64 is not optional -- MsgcoreCom, TargetCom
# and the cores underneath them are 64-bit, and AnyCPU would load a 32-bit CLR
# under WOW64 on some hosts and fail CoCreateInstance with a class-not-registered
# that has nothing to do with registration.
#
# THE STAGING LIST IS THE POINT OF THE SECOND HALF OF THIS FILE, and it is the
# same argument ComExamples\run_all.ps1 makes at length: two COM
# servers over one core must be loaded from ONE directory, because COM loads an
# in-proc server with LOAD_WITH_ALTERED_SEARCH_PATH and each server would
# otherwise resolve its own copy of Msgcore.dll -- two modules, two CRT states,
# two heaps, and a block freed in the wrong one.
#
#   .\build.ps1
#   .\build.ps1 -Config Release
#   .\build.ps1 -Clean

[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')] [string] $Config = 'Debug',
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$mscs = Resolve-Path (Join-Path $here '..\..')   # dotNetExamples\ -> repo root -> MSCS
$bin  = Join-Path $here "out\x64\$Config"

if ($Clean -and (Test-Path $bin)) { Remove-Item -Recurse -Force $bin }

# ---- the compiler ---------------------------------------------------------
# Roslyn as shipped with Visual Studio; falls back to the in-box framework
# compiler, which is older but compiles these sources unchanged.
$csc = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -property installationPath 2>$null | Select-Object -First 1
    if ($vs) {
        $candidate = Join-Path $vs 'MSBuild\Current\Bin\Roslyn\csc.exe'
        if (Test-Path $candidate) { $csc = $candidate }
    }
}
if (-not $csc) {
    $candidate = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
    if (Test-Path $candidate) { $csc = $candidate }
}
if (-not $csc) { throw "no C# compiler found (looked for Roslyn in VS, then Framework64)" }

# ---- the reference assemblies ---------------------------------------------
$fw = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319"
foreach ($asm in 'mscorlib.dll','System.dll','System.Core.dll') {
    if (-not (Test-Path (Join-Path $fw $asm))) { throw "missing $asm -- is .NET Framework 4.x installed?" }
}
$refs = @('mscorlib.dll','System.dll','System.Core.dll') | ForEach-Object { "/r:$(Join-Path $fw $_)" }

New-Item -ItemType Directory -Force -Path $bin | Out-Null

# P2PBridge.cs is in every harness even though only two use it: it costs a few
# kilobytes of IL and keeps the command line one shape.
$common = @(
    (Join-Path $here 'common\MsgcoreComInterop.cs')
    (Join-Path $here 'common\ComHarness.cs')
    (Join-Path $here 'common\P2PBridge.cs')
)

# Reading order, which is also dependency order.
$harnesses = @(
    'DataFieldTest', 'ListVectTest', 'MgrPersistTest', 'MgrCApiTest',
    'WsaStoreTest', 'WsaQueryTest', 'RecursTimeTest', 'BstrWidthTest'
)

$debugFlags = if ($Config -eq 'Debug') { @('/debug+','/optimize-','/define:DEBUG;TRACE') }
              else                     { @('/debug:pdbonly','/optimize+','/define:TRACE') }

Write-Host "compiling $Config with $csc"
$failed = 0
foreach ($name in $harnesses) {
    $src = Join-Path $here "$name\$name.cs"
    if (-not (Test-Path $src)) { throw "missing $src" }
    $out = Join-Path $bin "$($name)Net.exe"

    & $csc /nologo /noconfig /nostdlib+ /platform:x64 /langversion:latest /warn:4 `
           /target:exe /utf8output @debugFlags @refs "/out:$out" @common $src
    if ($LASTEXITCODE -ne 0) { Write-Host "  FAILED $name"; $failed++ }
}
if ($failed) { throw "$failed harness(es) failed to compile" }

# ---- stage the runtime DLLs ------------------------------------------------
# The exes reference NONE of these at compile time -- they are found in the
# registry at run time -- but the servers and their dependencies have to be on
# one search path, and run_all.ps1 registers the STAGED copies so this directory
# is self-contained.
#
# Msgcore.dll comes from ITS OWN BUILD OUTPUT and not from the shared
# bin\<Config>64 folder, which nothing deploys to automatically. Staging from
# there means a rebuilt core silently does not reach these harnesses, and the
# symptom is `regsvr32 ... failed with 3` rather than a missing symbol.
#
# MsgcoreCom moved to MsgFacade\com when it was ported off Msgcore's flat C ABI
# onto MsgFacade, so there is one more DLL in the chain than there used to be:
#
#       MsgcoreCom.dll  ->  MsgFacade.dll     ->  Msgcore.dll
#       TargetCom.dll   ->  TargetFacade.dll  ->  TargetCore.dll  ->  Msgcore.dll
$coreBin       = if ($Config -eq 'Debug') { Join-Path $mscs 'bin\Debug64' } else { Join-Path $mscs 'bin\Release64' }
$msgcoreBin    = Join-Path $mscs "Msgcore\out\x64\$Config"
$msgFacadeBin  = Join-Path $mscs "MsgFacade\out\x64\$Config"
$msgcoreComDir = Join-Path $mscs "MsgFacade\com\out\x64\$Config"
$p2pComDir     = Join-Path $mscs "TargetFacade\out\x64\$Config"

$stage = @(
    (Join-Path $msgcoreComDir 'MsgcoreCom.dll')
    (Join-Path $msgFacadeBin  'MsgFacade.dll')
    (Join-Path $p2pComDir     'TargetCom.dll')
    (Join-Path $p2pComDir     'TargetFacade.dll')
    (Join-Path $msgcoreBin    'Msgcore.dll')
    (Join-Path $coreBin       'TargetCore.dll')
)

foreach ($dll in $stage) {
    if (-not (Test-Path $dll)) {
        throw ("missing $dll`n" +
               "Build MsgFacade\com\MsgcoreCom(2022).vcxproj and " +
               "TargetFacade\com\TargetCom(2022).vcxproj for $Config|x64 first " +
               "(ComExamples\run_all.ps1 does both).")
    }
    Copy-Item $dll $bin -Force
}

Write-Host "$Config : $($harnesses.Count) harnesses + $($stage.Count) staged DLLs -> $bin"
