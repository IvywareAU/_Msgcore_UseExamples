# `ComExamples` — the **Msgcore** examples, through COM

Eight console harnesses that do what
[`DirectExamples`](../DirectExamples) does — exercise the
Msgcore data model, node by node — but reach it through **COM interfaces**
instead of through C++.

This is the fourth pass over the same subject matter in this repository, and the
table says why each pass exists:

| Tree | Subject | Reached through |
| --- | --- | --- |
| [`_TargetCore_UseExamples\DirectExamples`](../../_TargetCore_UseExamples/DirectExamples) | moving messages between hubs | TargetCore, C++ |
| [`_TargetCore_UseExamples\ComExamples`](../../_TargetCore_UseExamples/ComExamples) | the same | `TargetCom`, COM |
| [`DirectExamples`](../DirectExamples) | **what is *in* a message** | Msgcore, C++ |
| **this tree** | the same | **`MsgcoreCom`, COM** |

**The distinguishing property:** these eight executables link **nothing of
MSCS**. Not `Msgcore.lib`, not MFC, not ATL — only `ole32`, `oleaut32`, `uuid`
and a generated type-library header. Every object they touch is found in the
**registry** at run time. Whatever they can do, a VBScript file, an Excel macro,
a PowerShell script or a .NET host can also do.

---

## Status

**18/18 green — Debug|x64 and Release|x64, 2131 checks per configuration, zero
warnings.**

```
DataFieldTestCom     exit=0   237 checks, 0 failed
ListVectTestCom      exit=0   221 checks, 0 failed
MgrPersistTestCom    exit=0   346 checks, 0 failed
MgrCApiTestCom       exit=0   154 checks, 0 failed
WsaStoreTestCom      exit=0    37 checks, 0 failed
WsaQueryTestCom      exit=0    36 checks, 0 failed
RecursTimeTestCom    exit=0   333 checks, 0 failed
BstrWidthTestCom     exit=0   767 checks, 0 failed
script\ps_client.ps1  exit=0     0 failed        (with -IncludeScripts)
```

> **THE SERVER MOVED, AND SO DID ITS CONTRACT.** `MsgcoreCom` used to live in
> `Msgcore\com` and sit on `Msgcore_c.h`, the kernel's flat C ABI. It is
> [`MsgFacade\com`](../../MsgFacade/com) now and sits on **MsgFacade**, and this
> tree was rewritten against the revised interface. What changed for a reader of
> these harnesses, in one place:
>
> | | |
> | --- | --- |
> | **paths** | every step is introduced by its separator — `""` is the root, `.window.width` a grandchild, `.Config.Window@Colour` an attribute. Reversible: `FieldAt` takes back exactly what `Path` gives out, which the old dotted chain did not, and an attribute has a path at all, which it did not. |
> | **live vs detached is gone** | `Item`, `IsLive` and `msgcDetached` are removed. Every node is a route re-resolved per call, so there is no copy to hand out and no silent lost write to guard against. |
> | **the value stack is four members of a node** | `PushValue` / `PopValue` / `DropValue` / `IsStacked`, in place of the `MsgStack` coclass. It NESTS, and `PopValue` answers whether it restored anything. |
> | **`Nullify` → `Clear`, `RenameFile` → `RenameRoot`** | one keeps the filename and leaves held references answering `msgcStale`; the other renames the ROOT NODE, where the old one was a `MoveFileEx`. |
> | **`RetypeChild` takes a type** | not a value. A retype seeds a zero by definition, so the value argument was always a fiction. |
> | **`Child` reaches a container** | it used to throw on one. `ChildList` / `ChildVect` are the typed spellings now, not the only road. |
> | **`Declare(update:=False)` onto an existing name is `msgcDeclare`** | it used to succeed and answer the existing child, which made *create* and *assign* indistinguishable at the call site. |
> | **new** | `IsValidName`, `ChildAt`, walker `Depth` / `Path`, `msgcPath`, `msgcLimit`. |

The ninth is a **late-bound PowerShell client** — no compiler, no header, no
import lib, no interop assembly, one line of binding
([`script\ps_client.ps1`](script/ps_client.ps1)). It is off by default because it
costs a second PowerShell to start, not because it is optional.

It measures three host differences that are worth knowing before writing any
script against this server, and none of them is a property of the server:

* **PowerShell does not honour `DISPID_VALUE`.** Its COM adapter binds by name
  and never invokes the default member, so a bare node compares unequal to its
  value and interpolates as the empty string. VBScript, VBA and Excel all print
  `1024`; PowerShell needs `.Value`.
* **A failed property GET is silently `$null`** — no error, no `$Error` entry,
  nothing for `-ErrorAction Stop` to stop on. A failed *method* call throws with
  the `msgc*` code intact, and so does a property *set*. Same failure, same
  server, two behaviours.
* **Neither sink can be authored from a script.** Events need an interop
  assembly for the coclass (`MsgcoreCom` ships a type library, not a PIA);
  `SetPagingSink` needs an object implementing `IMsgPagingSink`, and a
  `PSCustomObject` is not a COM object.

The same eight subjects are covered a fourth time, in C#, by
[`dotNetExamples`](../dotNetExamples) — which
declares the type library by hand and gets the two sink objects a PowerShell
script cannot author.

---

## The server this tree exercises

`MsgcoreCom` — `MSCS\MsgFacade\com\` — is an **ATL in-proc COM server over
MsgFacade**. It was designed before this tree existed (the `.idl`, `ComUtil.*`
and the module were already written); building these eight harnesses is what
finished it, and what turned a dozen of its design claims into measured facts.

It used to sit on the flat C ABI in `Msgcore_c.h` and live in `Msgcore\com`.
Moving it onto the facade removed two of the five departures it documented — the
node-path re-resolver and the live/detached split — because the layer below
solves both for every client rather than only for this one. This tree was
rewritten against the result, which is the honest cost of that decision and the
reason the check counts above are not the ones they were.

It contains **no MFC**. It sees the store only through `MsgFacade.h`, which is
the point: if a COM server can be built over that header without naming a single
MFC type, then the header really is a complete boundary rather than a partial
one. (The process still loads MFC at run time, because `Msgcore.dll` needs it —
a deployment fact about the dependency, not a compile-time coupling.)

```
MsgcoreCom.MsgStore          the one creatable object -- a store IS a document
  .Root / .FieldAt           -> IMsgFieldCom     a node: name, value, children
      .Attributes            -> IMsgAttrCom      the parallel '@' tree
      .Descendants           -> IMsgDescCom      the children, as a collection
      .Cursor                -> IMsgCursorCom    a position within a collection
      .List / .Vector        -> IMsgListCom / IMsgVectCom
      .Walker                -> IMsgRecursCom    a subtree from one flat loop
  _IMsgStoreEvents           OnChange / OnError  (connection point)
  .SetPagingSink             -> IMsgPagingSink   (registered, synchronous)
```

---

## The eight

Read them in this order. Each builds on the one before.

| # | Harness | Servers | Subject |
| - | ------- | ------- | ------- |
| 1 | `DataFieldTestCom` | MsgcoreCom | typed cells as one `Value` VARIANT, names, descendants, attributes, and the error channel |
| 2 | `ListVectTestCom` | MsgcoreCom | where lists and vectors stop, and the cursor that stands in for them |
| 3 | `MgrPersistTestCom` | MsgcoreCom | the store as a document: Save/Load, `P2Pos`, and change events |
| 4 | `MgrCApiTestCom` | MsgcoreCom | **the same store through `IDispatch` late binding** — what a script sees |
| 5 | `WsaStoreTestCom` | MsgcoreCom + TargetCom | a whole store shipped between two hubs over loopback TCP |
| 6 | `WsaQueryTestCom` | MsgcoreCom + TargetCom | the store stays put and is *queried* across the mesh |
| 7 | `RecursTimeTestCom` | MsgcoreCom | walking a whole subtree, and the timestamp cell as a `DATE` |
| 8 | `BstrWidthTestCom` | MsgcoreCom | the heap, and its one irreversible decision — the addressing width |

Harness **4** is the one that is *not* a transliteration of its namesake. The
original drives the store through a second **ABI** (the flat C one) to show what
a different calling convention preserves. This one does exactly that against the
second ABI that exists *here*: every interface is `dual`, so the same objects can
be reached by vtable or **late-bound by name**, and late binding is the only way
VBScript, VBA, classic ASP and most .NET hosts can reach them at all. Not one
line of it calls a vtable method.

Each harness's own header comment is its documentation — the source files are
heavily commented and carry what the C++ tree puts in per-harness `README.md`
files. This tree has one README, like [`_TargetCore_UseExamples\ComExamples`](../../_TargetCore_UseExamples/ComExamples).

---

## Building and running

```powershell
.\run_all.ps1                    # build both servers + eight harnesses, register, run, unregister
.\run_all.ps1 -Config Release
.\run_all.ps1 -NoBuild
```

`run_all.ps1` registers with `regsvr32 /s /n /i:user` — per-user, `HKCU` only, no
elevation — and always unregisters again.

**Prerequisites:** `Msgcore.dll` and `TargetCore.dll` in `..\..\bin\Debug64` /
`..\..\bin\Release64`, and the `TargetFacade` tree built (harnesses 5 and 6 need
`TargetCom`). `run_all.ps1` builds both COM servers itself.

Every harness prints UTF-16 (`_setmode(_O_U16TEXT)`), so read a redirected log
with `Get-Content -Encoding Unicode`.

### One directory, one `Msgcore.dll`

`run_all.ps1` **stages** `MsgcoreCom.dll`, `TargetCom.dll`, `TargetFacade.dll`,
`Msgcore.dll` and `TargetCore.dll` into `out\x64\<Config>` and registers the
staged copies. That is load-bearing, not tidiness — see finding 6.

### Exit codes

The sibling trees' contract, unchanged:

| Code | Meaning |
| ---- | ------- |
| `0` | success — every check passed |
| `1` | setup failure (a server is not registered) |
| `2` | an assertion fired (banner on stderr) |
| `3` | a check failed, or a networked harness timed out |

---

## What the writing of these examples turned up

Ten things. Six are defects or traps in code that already existed; four are
design consequences of the COM boundary that a caller has to know. Every one is
also documented at the point of use.

The pattern from the C++ tree repeated exactly: **these are the paths nothing had
ever executed.** `MsgcoreCom`'s object implementations did not exist until this
tree needed them, and writing the client is what turned the `.idl`'s claims into
facts — three of which were wrong.

### 1. A linked list or a vector cannot be *created* through the flat C ABI

`Msgcore_c.h` has `msgcore_list_from_field` and `msgcore_vect_from_field`, which
wrap a field that **already holds** a list or vect, and every `declare` in the
header makes a *data* node. There is no `msgcore_field_declare_list`, so nothing
above that header can produce the input those two functions need.

So `IMsgListCom` and `IMsgVectCom` are reachable only over a store **loaded from
a file that already contains one**. `ListVectTestCom` pins the boundary
precisely rather than pretending otherwise: `IsList` is `False` on every node
this tier can make, and `Field.List` answers `msgcNotList` instead of an object
whose `AddTail` would go nowhere.

### 2. `P3PmsgCurs::IsEoCursor()` is true **on** the last element, not past it

```cpp
// MsgCurs.cpp:661
return nItems <= 0 || m_nItem >= nItems - 1;
```

So the loop every C programmer writes —

```cpp
for ( curs.Seek(); !curs.IsEoCursor(); ++curs )     // WRONG
```

— silently visits every element **but the last**, with no error, no assert and
no short read. A three-child node enumerates as two. This cost real time here:
the first `_NewEnum` yielded 2 of 3 children and looked plausible.

`IMsgCursorCom.EndOfCursor` is therefore a **different predicate** from the flat
one, on purpose: it means "the cursor has walked off the end", so the natural
loop is the correct one. Position is kept as an index and compared against the
count; `IsEoCursor` is never consulted.

### 3. `Exists`, `SelectItem`, `Delete` and `child` disagree about dotted names

```
msgcore_field_exists      ( f, L"ShipTo.City" )   ->  1      PARSES the path
msgcore_field_select_item ( f, L"ShipTo.City" )   ->  NULL
msgcore_field_delete_item ( f, L"ShipTo.City" )   ->  0
msgcore_field_child       ( f, L"ShipTo.City" )   ->  a HOLLOW handle
```

The last is the dangerous one: `msgcore_field_child` does **not** answer `NULL`
for a name it cannot resolve. It wraps `SelectItem(name).r_Object()`, and
`SelectItem` answers an *empty* field for a miss — so what comes back is
non-NULL with no name and no value, and every read through it silently reads
nothing.

Which makes `if ( Exists(n) ) Child(n)` — the most natural two lines available —
succeed and yield a hollow object.

`MsgcoreCom` publishes one rule instead: `Child`, `Item`, `Exists`, `Declare`
and `Delete` take one **literal** name and never parse; `MsgStore.FieldAt` is
the path walker. Enforcing it means resolving and then checking the name that
came back (`HasChildStrict`, `ComField.cpp`), which is why `Exists` here can
disagree with `msgcore_field_exists`. Every hop of every path resolution is
verified the same way.

### 4. `msgcore_mgr_nullify` leaves the manager alive and unusable

```cpp
// P2PmsgMgr.cpp:98
if ( m_hMgr ) P2PmsgHeap_Close ( m_hMgr );
m_hMgr = 0;
```

The heap is closed and the handle dropped, but the `P2PmsgMgr` object survives
pointing at nothing. `msgcore_mgr_is_valid` still answers `1`, and the **next**
call through the manager — `GetRootname`, `Sizeof`, anything — dereferences the
closed heap and access-violates.

A store a *script* can drive must not have a method that arms that, so the
replacement happens one layer down: **`IMsgStoreCom::Clear`** (was `Nullify`)
reaches `MsgFacade`, which builds an equivalent empty manager at the width and
bounds the store was created with and swaps it in. Two consequences worth
knowing: the store **keeps its filename**, so a later `Save()` with no path
writes the emptied store back over the file it came from; and every node
reference a client is holding stays valid and answers **`msgcStale`**, which is
what "the store was emptied" should look like from a handle.

### 5. `msgcore_mgr_rename` renames the **file**, not the store

```cpp
// P2PmsgMgr.cpp:484
if ( m_strFilename.IsEmpty() || !lpszNewname ) return FALSE;
if ( !MoveFileEx ( m_strFilename, lpszNewname, ... ) ) return FALSE;
```

The name and its position in the header's serialisation group both read as a
rename of the store's own name. It is a `MoveFileEx` on disk: the argument is a
**path**, a store that has never been saved answers `FALSE`, and the
63-character node-name rule has nothing to do with it.

It was published here as **`RenameFile`** for exactly that reason, and the root
node's name could not be changed at this tier at all.

**Both halves of that are now fixed.** The file rename is **gone** — a host has
its own file API — and **`RenameRoot`** has arrived, which is the inverse of
`RootName` and is validated like any other name (`msgcName` for an unusable one).
`MsgFacade` found the defect the same way this tree did: its own
`IMsgStore::Rename` was calling the kernel's, and so answered an error for every
in-memory store while silently *moving the file* of a saved one.

### 6. Two COM servers over one core will load `Msgcore.dll` **twice**

The two networked harnesses load both servers into one process:

```
MsgcoreCom.dll  ->  MsgFacade.dll     ->  Msgcore.dll
TargetCom.dll   ->  TargetFacade.dll  ->  TargetCore.dll  ->  Msgcore.dll
```

COM loads an in-proc server with `LOAD_WITH_ALTERED_SEARCH_PATH`, so each server
resolves its dependencies from **its own directory** first. Register the two
where they were built and the loader finds a `Msgcore.dll` beside each and loads
**both**: same bytes, two modules, two C-runtime states, **two heaps**. A block
allocated inside one and freed inside the other trips
`_CrtIsValidHeapPointer` in the debug CRT — which is exactly how this surfaced —
and silently corrupts the heap in a release build.

`run_all.ps1` stages both servers and all three core DLLs into one directory, so
there is one `Msgcore.dll` on the search path and therefore one module.

### 7. `msgcore_mgr_create_nn` asserts on a zero initial size

`P2PmsgHeap_CreateBSTRio` (`MsgVBHeap.cpp:2516`) opens with
`ASSERT(nSizeInitial)`. A zero **maximum** genuinely means "the default" — it is
turned into the width's addressable limit — but a zero **initial** is a debug
break, and the header does not say so.

`CreateNew` substitutes `2024`, which is exactly what `P2PmsgMgr`'s default
constructor passes (`P2PmsgMgr.cpp:38`), so `CreateNew mode` and a plain
`CoCreateInstance` produce heaps of the same initial size and differ only in the
width that was asked for.

### 8. A deleted node's `P2Pos` must never be resolved

The `DELETE` trigger is fired from inside `P2PmsgHeap_FreeBSTRio`
(`MsgVBHeap.cpp:2847`) **after** the block has been freed and collated. Two
consequences:

* the node's path can never be captured for a delete event, because at the only
  moment it could be, the node is already gone — so `OnChange`'s `path` is
  **always empty** for `msgcTriggerDelete`;
* handing that `P2Pos` to `FieldAt` reaches
  `P2PmsgHeap_AssertValidAllocBSTRio` (`MsgVBHeap.cpp:1428`), which finds a
  freed block and trips `ASSERT(0)`.

There is no "is this position still allocated" predicate in the flat ABI, so no
layer above it can check on your behalf. **Treat a delete event's `p2pos` as an
identity to compare against what you armed, never as something to resolve.**

### 9. Msgcore's own path spelling is not the one `FieldAt` takes — *closed*

`P3Pmsg_GetPath` (`P2Pmsg.cpp:6390`) emits a **leading `.`** and names the
**root** as its first segment, so the node `FieldAt` called `config.width` was
`.P2PmsgMgr name.config.width` to `PathOf`. (The `":RootName"` form still in
that function is commented out and never emitted.) The two looked similar enough
to be mistaken for each other, and feeding `PathOf`'s answer back to `FieldAt`
found nothing — the help string had to say so.

**Both ends are the facade's grammar now**, so `PathOf` and `Field.Path` answer
the same string and `FieldAt` takes it back. That grammar introduces every step
with its separator — `""` is the root, `.config.width` a grandchild,
`.Config.Window@Colour` an attribute — which is what makes it reversible, and
what gives an attribute a path at all. `MgrPersistTestCom §3` pins the round
trip; the old asymmetry is what that section used to be about.

### 10. What automation does to an HRESULT, in both directions

Measured in `MgrCApiTestCom` section 6 rather than assumed:

* **Failures survive, and survive well.** Every one arrives as
  `DISP_E_EXCEPTION` with the `msgc*` code in `EXCEPINFO.scode` and a full
  sentence in `bstrDescription` — `Err.Number` and `Err.Description` exactly.
  This is strictly better than the C++ tier, where the corresponding finding is
  that *a type mismatch through the flat C API is unrecoverable*: the wrappers
  throw out of an `extern "C"` function and MSVC deletes the handler.
* **A success code other than `S_OK` does not.** `Cursor.Next` answers `S_FALSE`
  by vtable when it steps off the end; through `Invoke` a host sees plain
  success. Same effect as `P2PF_S_UNRELATED_LINK` in `TargetCom`, and the same
  remedy: publish the answer as a readable property (`EndOfCursor`, `Index` —
  which is `-1` past the end, a value no real position can be mistaken for).

---

## What does not cross the boundary, and why

This section used to list four absences. Three of them have since been closed
in `Msgcore_c.h` and are now covered rather than pinned — and the wording that
described them was itself part of the problem, because calling a plain C++
class "a class with no flat entry point" describes its absence, not an
obstacle to wrapping it.

| Was absent | Now |
| --- | --- |
| Creating a list or a vect | `Field.DeclareList` / `DeclareVect`, live. **2 §1–2** |
| `MsgStck` (the per-node saved value) | `Field.PushValue` / `PopValue` / `DropValue` / `IsStacked`. It was briefly an `IMsgStackCom` object; one saved pair living inside a node does not need a coclass. **7 §7** |
| `P2PmsgRecurs` (recursive walker) | `Field.Walker` → `IMsgRecursCom`, with real pruning, and `Depth` / `Path`. **7 §6** |
| Paging callbacks, `SafeRegistrationPush` | `Store.SetPagingSink` + `PageIn`/`PageOut`/`PushPaging`. **3 §N** |
| A live node from a collection or a `P2Pos` | every node is one. `Item`, `IsLive` and `msgcDetached` are gone with the distinction. **1 §2, 4 §4** |
| Renaming the root; asking whether a name is usable | `Store.RenameRoot`, `Store.IsValidName`. **3 §1** |

One genuine absence remains:

| Absent | Why | Where |
| --- | --- | --- |
| A free-standing `P3PmsgData` | there is nothing for it to be at this tier — a VARIANT **is** the detached data cell | 1 |

And two things that only exist *here*, because C had no use for them:
**`DISPID_VALUE`**, so a node reads as its own value, and **`DISPID_NEWENUM`**,
so `For Each child In field` works. Both are exercised in harness 4 §3.

### Four traps the new coverage turned up

* **`Field.Child` could not reach a list or a vect** — *closed*. It resolved
  through `SelectItem`, which *throws* on a container node (a list is not an
  "item"), so `Child("samples")` failed for a list that was plainly there and
  `ChildList` existed for that gap. `Child` reaches one now; `ChildList` /
  `ChildVect` are the **typed** spellings, answering `msgcNotList` /
  `msgcNotVect` for a name that is present but the other kind.
* **The walker is not re-resolved per call, and cannot be.** It holds one live
  cursor per pushed level; snapshotting it is the thing a walker exists not to
  do. A mutation on **another branch** no longer disturbs it — the cursors are
  re-derived positions, not raw addresses — but a mutation **inside the subtree
  being walked** is the caller's problem, and there is no error code for it
  because there is nothing deterministic to report. Finish the walk, then
  mutate.
* **An element write converts, it does not retype.** Storing `7.9` into an
  `Int32` cell yields `8` and the cell stays `Int32`. Otherwise a sequence's
  element types would depend on the order a caller happened to write them in.
* **Paging is the inverse of `OnChange` at every point.** It is raised on the
  accessing thread, under the store lock, with the core blocked waiting for the
  answer, and its return value *is* the answer. A handler must not block and
  must not re-enter beyond the subtree it was asked for. `OnChange` is the
  opposite on all four counts; modelling one on the other deadlocks.

### Three Msgcore defects fixed rather than routed around

Each was a complete, working implementation disabled by a leftover marker:

* **`P3PmsgList::Delete` opened with a bare `ASSERT(0)`.** The assertion was
  the only difference between a debug build, where deleting a list element was
  impossible, and a release one, where it worked correctly. Verified element by
  element before removing it.
* **`MsgStck` declared four constructors and defined one**, so
  `MsgStck oStck;` — the default-construct-then-`Connect` form that `Connect()`
  exists to serve — failed to *link* for every consumer.
* **`MsgStck::Push` and `::Pop` dereferenced their field with no null check**,
  unlike `Drop`, `Rename` and `r_item`, which all guard.

Two more were routed around, because they are shapes rather than slips:
`SelectItem` throwing on a container (above), and `P3PmsgDesc(P3PmsgField*)`
connecting with a zero list size, which leaves `GetCount` and `Exists` working
while the typed selectors fail — the same call succeeds on the field's own
`r_Desc()`. Every selector goes through a cursor instead.

---

## Layout

```
ComExamples\
  ComExamples(2026).sln     all eight
  run_all.ps1                                stage + register + run + unregister
  common\ComHarness.h                        the msgc:: client layer (plain COM only)
  common\P2PBridge.h                         the p2p:: half, for harnesses 5 and 6
  script\ps_client.ps1                       the late-bound client (-IncludeScripts)
  common\HarnessCom.props                    shared build settings
  <Harness>\<Harness>(2026).vcxproj
  out\x64\{Debug,Release}\                   exes + the STAGED servers
  logs\{Debug,Release}\                      one log per harness
```

`common\ComHarness.h` is the counterpart of
[`_TargetCore_UseExamples\ComExamples\common\ComHarness.h`](../../_TargetCore_UseExamples/ComExamples/common/ComHarness.h),
one tier down, and uses nothing but plain COM to do it — no ATL, no
`_com_ptr_t`, no `#import`. Every accessor in it is null-safe: a wrapper whose
creating call failed answers a benign default rather than faulting inside a
`CHECK`, which would lose the check, the line number and every section after it.

`common\P2PBridge.h` duplicates a small slice of the sibling tree's hub wrapper
rather than sharing it, because a tree of examples that cannot be built without
a *sibling* tree of examples is not an example of anything.
