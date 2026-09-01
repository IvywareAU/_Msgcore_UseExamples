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
# ps_client.ps1 -- the reason the COM layer exists, in two pages.
#
# No compiler, no header, no import lib, no type library reference, no interop
# assembly: a late-bound PowerShell client driving the same store the eight C++
# harnesses drive, through IDispatch and the registered coclass. One line of
# binding --
#
#       $store = New-Object -ComObject MsgcoreCom.MsgStore
#
# -- and everything after it is GetIDsOfNames and Invoke, by name, which is what
# ..\MgrCApiTestCom measures with an exit code.
#
# WHAT A SCRIPT GETS THAT A C++ CALLER PAYS FOR, and it is most of this file:
#
#   * DISPID_NEWENUM. `foreach ($c in $field)` walks the children, and because
#     the server snapshots rather than handing out a live cursor, the body may
#     delete as it goes. Section 3.
#   * Err. A failed call is a trappable error whose HResult carries the msgc*
#     code and whose Message carries the server's sentence -- so msgcDetached,
#     msgcStale and msgcNotVect are all things a script can branch on.
#   * Containers, the walker and the stack: everything Msgcore_c.h grew, with
#     no marshalling ceremony anywhere. Sections 5 and 6.
#   * No lifetime management at all. No AddRef, no Release, no VariantClear.
#
# AND THE THREE THINGS IT DOES NOT GET, all measured below rather than omitted:
#
#   * DISPID_VALUE, WHICH IS THE SURPRISE. The server marks Value as the default
#     member so that a node reads as its own value, and VBScript, VBA and Excel
#     all honour it. PowerShell does not: its COM adapter binds by name and
#     never invokes the default member, so a bare node compares unequal to its
#     value and interpolates as an empty string. Write .Value. Section 2.
#   * _IMsgStoreEvents. PowerShell CAN sink COM events through
#     Register-ObjectEvent, but only for a coclass it has an interop assembly
#     for; MsgcoreCom ships a type library, not a PIA.
#   * A paging sink, for the opposite reason: SetPagingSink wants an object
#     implementing IMsgPagingSink, and a PSCustomObject is not a COM object.
#
# The last two are HOST limitations, not gaps in the layer -- the C++ harnesses
# in this tree attach to the same connection point, and
# ..\..\dotNetExamples does both from C#. Section 7 says so once.
#
#   ..\run_all.ps1 -IncludeScripts      # registers, runs this, unregisters
#
# Exit code = number of failed checks.

[CmdletBinding()]
param([ValidateSet('Debug','Release')] [string] $Config = 'Debug')

$ErrorActionPreference = 'Stop'
$here   = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $here "..\out\x64\$Config"

# Msgcore.dll resolves against the PROCESS directory, so run from where the
# staged copies are.
Push-Location $binDir

$script:fails = 0
function Check($ok, $what) {
    if ($ok) { Write-Host "  ok    $what" }
    else     { Write-Host "  FAIL  $what"; $script:fails++ }
}
function Section($title) { Write-Host "`n--- $title" }
function Note($text)     { Write-Host "  $text" }

# The Err object, PowerShell-shaped. HResult is a signed Int32, so the
# formatting is what turns 0x8004030B back into something readable.
function ErrOf([scriptblock] $sb) {
    try { & $sb | Out-Null; return $null }
    catch {
        $e = $_.Exception
        while ($e.InnerException -and -not ($e -is [System.Runtime.InteropServices.COMException])) {
            $e = $e.InnerException
        }
        return [pscustomobject]@{
            Number      = '0x{0:X8}' -f $e.HResult
            Description = $e.Message
            Type        = $e.GetType().Name
        }
    }
}
function ShowErr($what, $e) {
    Write-Host "  $what -> $($e.Number) [$($e.Type)]"
    $d = $e.Description -replace "`r?`n", ' '
    if ($d.Length -gt 150) { $d = $d.Substring(0,147) + '...' }
    Write-Host "     $d"
}

$store = $null
try {
    # =====================================================================
    Section "1. One line of binding"
    # =====================================================================
    #
    # No CoCreateInstance to write, no IID, no type library. The ProgID is
    # resolved in HKCU\Software\Classes because run_all.ps1 registered the
    # server per-user -- which needs no elevation and is undone afterwards.
    $store = New-Object -ComObject MsgcoreCom.MsgStore
    Check ($null -ne $store) "New-Object -ComObject MsgcoreCom.MsgStore"
    Write-Host "        $($store.VersionString)"

    Check ($store.IsValid) "IsValid reads as a Boolean property"
    Check ($store.Filename -eq '') "a fresh store has never been saved"
    Check ($store.RootName.Length -gt 0) "but its root is named"
    Note "root name : '$($store.RootName)'  heap: $($store.Size) bytes"

    # =====================================================================
    Section "2. Building a tree -- and the one script idiom PowerShell does NOT get"
    # =====================================================================
    $root = $store.Root
    $win  = $root.Declare("window", 0, $true)
    $null = $win.Declare("width",  1024, $true)
    $null = $win.Declare("height", 768,  $true)
    $null = $win.Declare("title",  "Ivyware € Chartboard", $true)

    Check ($win.Count -eq 3) "three children declared, entirely late-bound"
    Check ($store.FieldAt(".window.width").Value -eq 1024) "and read back by path"
    Check ($store.FieldAt(".window.title").Value -eq "Ivyware € Chartboard") `
          "...including a wide string, U+20AC intact end to end"

    # ---------------------------------------------------------------------
    # DISPID_VALUE IS NOT FREE IN POWERSHELL, and this is the place to say so
    # rather than to write `$store.FieldAt(".window.width")` and be quietly
    # wrong.
    #
    # The server marks Value as DISPID_VALUE precisely so that a node reads as
    # its own value. VBScript, VBA and Excel honour that: `WScript.Echo
    # store.FieldAt(".window.width")` prints 1024. PowerShell DOES NOT. Its COM
    # adapter binds members by name and never invokes the default member for
    # you, so the bare node is an RCW: it compares unequal to 1024 and
    # interpolates as the empty string.
    #
    # That is a HOST difference, not a server one -- the dispid is right, the
    # C++ and C# harnesses both read it at DISPID_VALUE, and ..\MgrCApiTestCom
    # invokes it by id and gets 1024. It is measured here because it is the
    # single most likely thing for a script author to assume.
    $bare = $store.FieldAt(".window.width")
    Check (-not ($bare -eq 1024)) "PowerShell does NOT unwrap DISPID_VALUE in a comparison"
    Check ("$bare" -eq '') "...and interpolates the node as an empty string"
    Note "So write .Value in PowerShell. VBScript and VBA would not have to."

    # A write through the named property, which is the same slot.
    $store.FieldAt(".window.width").Value = 1280
    Check ($store.FieldAt(".window.width").Value -eq 1280) "writing through a live node lands"

    # The declared WIDTH is the one thing a VARIANT cannot say, so it has its
    # own verb -- and it survives, which is why a config round trip through a
    # script does not silently rewrite the file's types.
    $net = $root.Declare("network", 0, $true)
    $null = $net.Declare("host", "127.0.0.1", $true)
    $null = $net.DeclareTyped("port", 7788, 4, $true)      # 4 = msgcTypeUInt16
    Check ($net.Child("port").TypeName -eq "UINT16") "DeclareTyped set an explicit width"
    Check ($net.Child("port").Value -eq 7788) "with its value"

    # =====================================================================
    Section "3. For Each -- DISPID_NEWENUM, and a snapshot"
    # =====================================================================
    #
    # `foreach ($c in $field)` is what a host compiles into
    # QueryInterface(IEnumVARIANT) on _NewEnum. Its contract is one pass over a
    # FIXED SET, which is why the server snapshots rather than handing out a
    # live cursor -- deleting as you walk is the commonest thing a foreach body
    # does, and a live position does not survive it.
    $names = @()
    foreach ($child in $store.FieldAt(".window")) { $names += $child.Name }
    Check ($names.Count -eq 3) "foreach walked the children"
    Check (($names -join ' ') -eq 'width height title') "in declaration order"
    Note "foreach : $($names -join ' ')"

    # And the elements are LIVE, so a foreach is also an edit pass.
    foreach ($child in $store.FieldAt(".window")) {
        if ($child.TypeName -eq 'INT32') { $child.Value = $child.Value * 2 }
    }
    Check ($store.FieldAt(".window.width").Value -eq 2560) "and the elements are live, so it can edit"
    Check ($store.FieldAt(".window.height").Value -eq 1536) "...every one of them"

    # Deleting while walking, which only works because it is a snapshot.
    $bin = $root.Declare("inbox", 0, $true)
    0..7 | ForEach-Object { $null = $bin.Declare("msg$_", $_, $true) }
    $visited = 0
    foreach ($m in $store.FieldAt(".inbox")) {
        $visited++
        if ($m.Value % 2 -eq 0) { $null = $store.FieldAt(".inbox").Delete($m.Name) }
    }
    Check ($visited -eq 8) "every element was visited despite the deletes"
    Check ($store.FieldAt(".inbox").Count -eq 4) "and four survived"

    # =====================================================================
    Section "4. Live vs detached -- the trap this layer no longer has"
    # =====================================================================
    #
    # This section used to demonstrate the trap and now demonstrates its
    # absence. Two families below this tier answered the same handle type: one
    # aliased the live tree, the other deep-copied, and a write through the copy
    # SUCCEEDED and reached nothing -- discovered hours later as a Save that did
    # not contain the change. `Item`, `IsLive` and msgcDetached surfaced it as
    # something a script could read and trap, which was the best that could be
    # done from up here.
    #
    # A node is a ROUTE now, re-resolved per call, so every reference is live and
    # all three members are GONE rather than kept as ceremony.
    $byChild = $store.FieldAt(".window").Child("title")
    $byPath  = $store.FieldAt(".window.title")
    Check ($null -eq ($byChild | Get-Member -Name 'Item'   -ErrorAction SilentlyContinue)) "Field.Item is gone"
    Check ($null -eq ($byChild | Get-Member -Name 'IsLive' -ErrorAction SilentlyContinue)) "Field.IsLive is gone with it"

    $byChild.Value = "Chartboard II"
    Check ($byPath.Value -eq "Chartboard II") "a write through one reference is seen by the other"

    $byPath.Value = "Chartboard III"
    Check ($byChild.Value -eq "Chartboard III") "...in both directions"
    Check ($store.FieldAt('.window.title').Value -eq "Chartboard III") "and by a third taken afterwards"

    # The collection's Item is live too, which it also was not.
    $viaColl = $store.FieldAt(".window").Descendants($true).Item("title")
    Check ($viaColl.Value -eq "Chartboard III") "a collection member reads the same node"
    $viaColl.Value = "Chartboard II"
    Check ($store.FieldAt('.window.title').Value -eq "Chartboard II") "and writes through to it"

    # =====================================================================
    Section "5. Containers -- which Child could not reach, and now can"
    # =====================================================================
    #
    # DeclareList / DeclareVect are the only way to bring a container into
    # existence from this tier at all. ChildList / ChildVect used to be the only
    # way to reach one afterwards, because Child resolved through SelectItem and
    # SelectItem THROWS on a container node -- a list is not an "item", so
    # `Child("samples")` failed for a list that was plainly there. Child reaches
    # it now, and the two typed spellings are a convenience rather than the only
    # road.
    $bag  = $root.Declare("bag", 0, $true)
    $list = $bag.DeclareList("samples")
    $list.AddTail(11)
    $list.AddTail(22)
    $list.AddHead(0.5)
    Check ($list.Count -eq 3) "a list created and filled from a script"
    Check ($list.Item(0) -eq 0.5) "indexed, and typed as stored"
    Check ($list.Item(2) -eq 22) "...at both ends"
    Note "Item takes an index, so it is a METHOD call here and needs no default member."

    # The handle is LIVE, not a private copy: reach the node again by name.
    $back = $bag.ChildList("samples")
    Check ($back.Count -eq 3) "ChildList reaches the same list"

    $asNode = $bag.Child("samples")
    Check ($null -ne $asNode) "Child CAN reach it now"
    Check ($asNode.IsList) "and reports it as a list"
    Check ($asNode.Name -eq 'samples') "with its name"

    $e = ErrOf { $null = $bag.ChildVect("samples") }
    Check ($e.Number -eq '0x80040308') "and asking for the wrong kind is msgcNotVect, not silence"

    # A vect, and the Count that only exists because the flat ABI grew one.
    $vect = $bag.DeclareVect("payload", 3, 5)              # 5 = msgcTypeInt32
    $vect.Item(0) = 5
    $vect.Item(1) = 6
    $vect.Item(2) = 7
    Check ($vect.Count -eq 3) "a vect created and counted"
    Check ($vect.Item(1) -eq 6) "and indexed"

    $sum = 0
    foreach ($x in $vect) { $sum += $x }
    Check ($sum -eq 18) "foreach over a vect reads the elements, not the positions"

    # An element write CONVERTS to the declared type, it does not retype the
    # cell. Otherwise a sequence's element types would depend on the order a
    # caller happened to write them in.
    $vect.Item(0) = 7.9
    Check ($vect.TypeAt(0) -eq 5) "7.9 into an Int32 cell leaves it Int32"
    Check ($vect.Item(0) -eq 8) "...having converted, not truncated"

    # =====================================================================
    Section "6. The walker and the stack -- the two objects that hold state"
    # =====================================================================
    #
    # Everything else in this server re-resolves its node on every call, which
    # is what makes a held reference survive a heap relocation. These two
    # cannot: a walker IS a chain of live cursors and a stack IS a position, so
    # both hold a live handle and answer msgcStale if the tree moves under them.
    # Finish the walk, then mutate.
    $w = $root.Declare("walk", 0, $true)
    $keep = $w.Declare("keep", 1, $true)
    $null = $keep.Declare("k1", 11, $true)
    $null = $keep.Declare("k2", 12, $true)
    $skip = $w.Declare("skip", 2, $true)
    $null = $skip.Declare("s1", 21, $true)
    $null = $w.Declare("leaf", 3, $true)

    # The prune is the ABSENCE of a Push -- "skip" is still visited, only its
    # children are not. That is the one thing a foreach cannot express.
    $walker = $store.FieldAt(".walk").Walker
    $seen = @(); $guard = 0
    while (-not $walker.AtEnd -and $guard -lt 100) {
        $guard++
        $seen += $walker.Name
        if ($walker.IsField -and $walker.Name -ne 'skip') { $null = $walker.Push() }
        $walker.MoveNext()
    }
    Check ($seen -contains 'skip') "'skip' was visited"
    Check (-not ($seen -contains 's1')) "but not descended into"
    Check ($seen.Count -eq 5) "so 5 stops instead of 6"
    Note "walk : $($seen -join ' ')"

    # The walker reports its own depth and path now, which it could not before.
    # Path is assembled as the walk moves, so it stays true after the walk has
    # gone past -- unlike Name, which is where the walker IS.
    $walker2 = $store.FieldAt(".walk").Walker
    Check ($walker2.Depth -eq 0) "a fresh walker is at depth 0"
    Check ($walker2.Path -eq '.walk.keep') "and reports the path of its first stop"
    $null = $walker2.Push()
    Check ($walker2.Depth -eq 1) "Push descends a level"

    # A mutation on ANOTHER branch does not disturb it: the cursors are
    # re-derived, not raw addresses. Inside the subtree being walked it is the
    # caller's problem -- a walker is not a snapshot, which is the point of it.
    $null = $root.Declare("disturb", 1, $true)
    Check ($walker2.Depth -eq 1) "and a mutation elsewhere leaves it where it was"
    $null = $root.Delete("disturb")

    # THE VALUE STACK, which used to be an object of its own -- $f.Stack, with a
    # Push/Pop/Drop/IsEmpty surface over the core's MsgStck. What that object was
    # is ONE saved (name, value) pair living inside the node, so it is four
    # members of the node: no second object and no second lifetime to get wrong.
    $node = $store.FieldAt(".walk")
    Check (-not $node.IsStacked) "nothing stacked to begin with"
    $node.PushValue()
    Check ($node.IsStacked) "PushValue saved the pair"
    $node.Value = 99
    Check ($node.Value -eq 99) "the node can then be overwritten"
    Check ($node.PopValue()) "PopValue ANSWERS whether it restored anything"
    Check ($node.Value -eq 0) "and it did"
    Check (-not $node.PopValue()) "a second pop answers False rather than failing"
    Note "That answer is what makes a drain loop terminate."

    # And it NESTS -- push, push, pop, pop unwinds. Every tier here documented
    # the opposite until a client pushed twice: the kernel merely ASSERTED that
    # the saved slot was empty while implementing a linked stack.
    $node.Value = 1
    $node.PushValue(); $node.Value = 2
    $node.PushValue(); $node.Value = 3
    $null = $node.PopValue()
    Check ($node.Value -eq 2) "one pop unwinds one level"
    $null = $node.PopValue()
    Check ($node.Value -eq 1) "...and the next unwinds the one below it"
    Check (-not $node.IsStacked) "leaving nothing stacked"

    # =====================================================================
    Section "7. Save, load, and the two things a script cannot do"
    # =====================================================================
    $file = Join-Path $binDir 'ps_client.p2p'
    if (Test-Path $file) { Remove-Item $file -Force }

    $store.Save($file)
    Check ($store.Filename -eq $file) "Filename followed the Save"
    Check (Test-Path $file) "and the file is on disk"

    $other = New-Object -ComObject MsgcoreCom.MsgStore
    $other.Open($file)
    Check ($other.FieldAt(".window.width").Value -eq 2560) "a second store read it back"
    Check ($other.FieldAt(".network.port").TypeName -eq "UINT16") "with the declared width intact"
    Check ($other.RootName -eq $store.RootName) "the root name came back with it"

    # THE FIRST THING A SCRIPT HAS TO BE TOLD. P2Pos is a `hyper`, and it comes
    # back to PowerShell as an Int64 without trouble -- but a bare integer
    # LITERAL in PowerShell is an Int32, so passing one where a hyper belongs is
    # a type mismatch at the marshaller. Round-tripping the store's own answer
    # is fine; typing a number is not.
    $pos = $store.FieldAt(".window.width").P2Pos
    Check ($pos -is [long]) "P2Pos comes back as an Int64"

    # PathOf AND Path ARE ONE SPELLING NOW, and FieldAt takes it back. They were
    # not: PathOf answered the kernel's own grammar, with the root's NAME as its
    # first segment, so a script that logged one and resolved the other got a
    # silent miss and the help string had to warn about it.
    Check ($store.PathOf($pos) -eq '.window.width') "PathOf answers the published grammar"
    Check ($store.PathOf($pos) -eq $store.FieldAt('.window.width').Path) "...the same one Path does"
    Check ($store.FieldAt($store.PathOf($pos)).Value -eq 2560) "and FieldAt takes it straight back"
    Note "PathOf = Path = '$($store.PathOf($pos))'"

    # A P2Pos is writable too, which it was not: it used to answer a detached
    # copy, because the deep copy was the only thing below this tier to hand back.
    $store.FieldAt($pos).Value = 2561
    Check ($store.FieldAt('.window.width').Value -eq 2561) "a node reached by P2Pos is writable"
    $store.FieldAt($pos).Value = 2560

    # THE SECOND. This script does NOT sink _IMsgStoreEvents. PowerShell binds
    # COM events through Register-ObjectEvent, which needs an interop assembly
    # for the coclass -- MsgcoreCom ships a type library, not a PIA. The C++
    # harnesses in this tree attach to the same connection point, and so does
    # dotNetExamples, which declares the dispinterface in C#. It
    # is a host limitation, not a gap in the layer.
    #
    # What a script CAN drive is the SYNCHRONOUS half -- and it turns out it
    # cannot drive that either, for the opposite reason: SetPagingSink needs an
    # object implementing IMsgPagingSink, and a PSCustomObject is not a COM
    # object. What it CAN do is observe that a store with no sink pages
    # harmlessly, which is the ordinary case.
    $paged = $root.Declare("paged", 0, $true)
    Check ($store.PageIn($paged.P2Pos) -is [bool]) "PageIn on a store with no sink is a no-op, not an error"
    Note "Events and paging sinks both need an object a script cannot author:"
    Note "see ..\\..\\dotNetExamples, where C# can."

    # Tidy up. Close is about WHEN, not whether: the store owns everything it
    # handed out, so releasing the last reference would do the same.
    $other.Close()
    $store.Close()
    Check (-not $store.IsValid) "Close destroyed the store"

    # THE THIRD HOST DIFFERENCE, and the sharpest one in this file: PowerShell
    # reports a failed COM call in TWO different ways depending on how the
    # member is spelled.
    #
    #   a failed METHOD call     -> throws, and the COMException carries msgcClosed
    #   a failed PROPERTY GET    -> SILENTLY $null. No error, no $Error entry,
    #                               nothing for -ErrorAction Stop to stop on.
    #
    # (A property SET does throw -- section 4 relies on it for msgcDetached.)
    # So `if ($null -eq $store.Root)` is the only way to notice that the getter
    # failed, and a script that reads a property off a store someone else closed
    # gets $null and carries on. The server behaved identically in both cases;
    # this is entirely PowerShell's COM adapter.
    $rootAfterClose = $store.Root
    Check ($null -eq $rootAfterClose) "a failed property GET is SILENTLY `$null in PowerShell"

    $e = ErrOf { $null = $store.FieldAt(".window") }
    Check ($e.Number -eq '0x80040300') "while a failed METHOD call throws msgcClosed"
    ShowErr "a method call on a closed store" $e
    Note "Same failure, same server, two host behaviours. Check `$null after a getter."

    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($other)
    Remove-Item $file -Force -ErrorAction SilentlyContinue
}
finally {
    if ($null -ne $store) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($store)
    }
    Pop-Location
}

Write-Host "`n$script:fails failure(s)"
exit $script:fails
