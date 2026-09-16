# `DirectExamples` — worked examples for the **Msgcore** library

Eight single-file console harnesses for `MSCS/Msgcore` — the message **content**
library: a self-describing tree of named, typed cells packed into a
relocation-safe heap that can be saved, addressed by position, and shipped.

This tree exists because the sibling [`_Targetcore_UseExamples\DirectExamples`](../../_Targetcore_UseExamples/DirectExamples)
does **not** cover it. Those eleven harnesses are Targetcore examples: they link
`Msgcore.lib` and include one Msgcore header (`Msgexception.h`, unused), but they
never touch `P3PmsgData`, `P3PmsgField`, `P3PmsgList`, `P3PmsgVect`, `P2PmsgMgr`
or `Msgcore_c.h`. Their payloads are opaque byte ranges. This tree starts where
those stop.

| | `_Targetcore_UseExamples\DirectExamples` | `DirectExamples` |
| --- | --- | --- |
| Subject | moving messages between hubs | what is *in* a message |
| Written against | `P2PeerHub`, `P2PeerCon`, `P2PeerMsg` | `P3Pmsg*`, `P2PmsgMgr`, `Msgcore_c.h` |
| Links | `Targetcore.lib` + `Msgcore.lib` | `Msgcore.lib` (+ `Targetcore.lib` for the last two) |
| Networking | every harness | the last two only |

The two networked harnesses use the **`WsaMeshTest` mesh**, as asked: two
`P2PeerHub`s in one process, each on its own `SpawnHub()` pump thread, connected
over a loopback TCP socket (`P2PeerConWsa` on `127.0.0.1`). For anything about
the mesh itself — hubs vs pumps, thread affinity, the login handshake, what
happens to an exception in a handler — read
[`../../_Targetcore_UseExamples/ArchitectureFAQ.md`](../../_Targetcore_UseExamples/ArchitectureFAQ.md);
none of it is re-explained here.

---

## The eight

Read them in this order. Each builds on the one before.

| # | Harness | Links | Subject |
| - | ------- | ----- | ------- |
| 1 | [`DataFieldTest`](DataFieldTest) | Msgcore | `P3PmsgData` typed cells, `P3PmsgField` = name + data, attributes, descendants, `MsgStck`, and `P2Pevent` as the error channel |
| 2 | [`ListVectTest`](ListVectTest) | Msgcore | `P3PmsgList` and `P3PmsgVect`, the 32-element spill seam, nesting, and `P3PmsgCurs` generic traversal |
| 3 | [`MgrPersistTest`](MgrPersistTest) | Msgcore | `P2PmsgMgr`: one heap per tree, `Save`/`Load`, `P2Pos` addressing, path resolution, headless change triggers |
| 4 | [`MgrCApiTest`](MgrCApiTest) | Msgcore | the same store through the flat C API (`Msgcore_c.h`) — live vs detached handles, handle ownership, and where the wrappers are not exception-safe |
| 5 | [`WsaStoreTest`](WsaStoreTest) | Msgcore + Targetcore | a whole `P2PmsgMgr` store serialised, sent between two hubs over loopback TCP, and rebuilt on the far side |
| 6 | [`WsaQueryTest`](WsaQueryTest) | Msgcore + Targetcore | the store stays put and is *queried* across the mesh — `BEGIN_P2PeerMsg_MAP` request/response over `RootPath2Object` |
| 7 | [`RecursTimeTest`](RecursTimeTest) | Msgcore | `P2PmsgRecurs`, the recursive subtree walker, and `P3PmsgTime`, the TIME64 cell |
| 8 | [`BstrWidthTest`](BstrWidthTest) | Msgcore | `P3PmsgBSTR` — the heap under everything — its addressing width, and the paging callbacks |

Harnesses 7 and 8 exist to close the coverage gap: between them the eight now
reach every class in Msgcore that a caller can actually call. What they do
**not** cover is deliberate — `P2Ptype`, `P2Pc_str`, `P2Pc_vBlob`, `P2Pint__`,
`P2PSafePtr`, `P2PSafeLock`, the `P2Psafe*` Win32 wrappers and the
`P2PmsgCheckMemory`/`P2PmsgCheckPtrs` debug helpers carry no `Msgcore_EXT`, so
they are not exported from the DLL and a consumer cannot reach them; the
`VBLock*` structs are the on-heap layout, below the API; and `CMsgcore`
(`Msgcore.h:94`) is still the AppWizard stub, its body a `// TODO`.

### The four ideas, once

```
P3PmsgData    a typed value cell        int / double / wstr / blob / GUID
P3PmsgName    a bounded name            63 UTF-16 units, stored inline
P3PmsgField   name + data               plus three optional side-cars:
                .r_Desc()                 DESCENDANTS -- the child tree
                .r_Attr()                 ATTRIBUTES  -- a parallel @-keyed tree
                .r_Stck()                 a value STACK -- push / mutate / pop
P2PmsgMgr     a heap that owns a tree   Save/Load, P2Pos, triggers
```

`P3PmsgItem` is a `typedef` of `P3PmsgField` (`P2Pmsg.h:746`); `P3PmsgList`,
`P3PmsgVect` and `P2PmsgMgr` all *derive* from it. There is exactly one node
type in the model — which is why a vect can hold a vect, a manager can be
declared into like any field, and one cursor walks all of it.

---

## Building and running

Same shape as the sibling tree: one solution at the root that builds all eight,
and a single shared `out\` root. Build a single harness with MSBuild's `/t:` on
that solution rather than looking for a per-project `.sln` — there isn't one.

```
DirectExamples\
  DirectExamples(2026).sln          all eight
  <Harness>\<Harness>(2026).vcxproj
  out\x64\{Debug,Release}\                exes + staged DLLs
  out\x64\{Debug,Release}\obj\<Harness>\  intermediates
```

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild ".\DirectExamples(2026).sln" /p:Configuration=Debug /p:Platform=x64
& $msbuild ".\DirectExamples(2026).sln" /t:DataFieldTest /p:Configuration=Debug /p:Platform=x64
.\out\x64\Debug\DataFieldTest.exe
```

**Prerequisite:** `Msgcore.dll` in `..\..\bin\Debug64` / `..\..\bin\Release64`, and
`Targetcore.dll` too for harnesses 5 and 6. The post-build step stages them next
to the exe and *fails the build* if they are missing — `xcopy` exits 0 on a
wildcard miss, so without that check a missing DLL only shows up as a
`0xC06D007E` at startup.

Every harness prints its output as UTF-16 (`_setmode(_O_U16TEXT)`), so from
PowerShell read redirected output with `Get-Content -Encoding Unicode`.

[`run_all.ps1`](run_all.ps1) builds the tree and runs all eight, then prints a
pass/fail table:

```powershell
.\run_all.ps1                     # build + run Debug
.\run_all.ps1 -Config Release
.\run_all.ps1 -NoBuild
```

### Exit codes

The sibling tree's contract, unchanged:

| Code | Meaning |
| ---- | ------- |
| `0` | success — every check passed |
| `1` | setup failure (startup / factory) |
| `2` | an MFC/CRT assertion fired (banner on stderr) |
| `3` | a check failed, or a networked harness timed out |

Current status — **16/16 green**, Debug and Release, 405 checks per configuration:

```
Debug    DataFieldTest    exit=0   59 checks, 0 failed
Debug    ListVectTest     exit=0   71 checks, 0 failed
Debug    MgrPersistTest   exit=0   58 checks, 0 failed
Debug    MgrCApiTest      exit=0   75 checks, 0 failed
Debug    WsaStoreTest     exit=0   13 checks, 0 failed
Debug    WsaQueryTest     exit=0   18 checks, 0 failed, 5/5 answered
Debug    RecursTimeTest   exit=0   61 checks, 0 failed
Debug    BstrWidthTest    exit=0   50 checks, 0 failed
Release  (identical)
```

---

## What the writing of these examples turned up

Nine things that are not in any header comment and that will cost someone an
afternoon. Each is documented at the point of use in the harness that hit it.

Seven of the nine were **defects in Msgcore itself, and have been fixed**
(2026-08-09), with guarding cases added to `MscsUnitTests/MsgcoreSuite.cpp` —
now 210 cases / 1311 checks. They are kept here because any Msgcore build older
than that date still has them, and because each one says something about how the
library is put together.

The pattern behind most of them is worth stating once: **these are the paths
nothing had ever executed.** `P3PmsgList::GetPrev`, `P3PmsgData::c_time()`,
`P3PmsgBSTR::IsDirty()`, `P2PmsgMgrnn`'s default constructor, `P3PmsgTime` as a
whole, and 64-bit `P3PmsgBSTR` heaps had no callers anywhere in the repository —
not in the library, not in `MscsUnitTests`, not in Targetcore. Writing an
example that calls a thing is what turns a declaration into a fact.

### 1. `P3PmsgList::AddListHead` corrupted the item it added — FIXED

`AddListHead` omitted the `VBLockItem_Init(..., VBLock_Data)` call that its twin
`AddListTail` makes. The item was linked into the list with no type tag, so the
next read of it fell through `VBLockItem_pData` to its `ASSERT(0)`
(`P2PmsgVBLock.cpp:4046`) — a debug assert, and undefined behaviour without one.

The initialiser is now present and the two functions differ only in which end
they link to. Against an older Msgcore, prepending is unavailable: build the list
in the order you want it, or `AddListTail` into a fresh list.

### 2. `P3PmsgList::GetPrev` did not exist — FIXED

Declared at `MsgList.h:83` as the mirror of `GetNext` but never defined in
`MsgList.cpp`, so calling it was an unresolved external at **link** time, not a
compile error.

It is now implemented as that mirror. The items were doubly linked all along —
`VBLockItem_GetPrev` is what `P3PmsgCurs` and the Desc/Attr containers already
used — so nothing about the format changed; the list simply had no public
backward walk. Start at `GetTailPos()`; `GetPrev` steps back over the cell it
returns and the position falls to `0` past the head.

### 3. The Msgcore heap image cannot be reached in memory

A `P2PmsgMgr`'s heap is already one contiguous, relocation-safe image — that is
why `Save()`/`Load()` can be a plain block write. But the accessors for it
(`P2PmsgHeap_pImage`, `P2PmsgHeap_pIOmage`, `P2PmsgHeap_CreateIOMAGE`,
`MsgVBHeap.h:118-157`) carry **no `Msgcore_EXT`**, so they are not exported from
the DLL and no client can call them. `P2PmsgMgr::m_hMgr` is `protected`, so
there is no way around it from outside either.

The exported route to the same bytes is `Save()` to a file and read the file.
`WsaStoreTest` does exactly that, and the extra round trip through the
filesystem is a limitation of the export surface, not of the format. Exporting
those five functions would remove it.

### 4. A type mismatch through the C API is unrecoverable, not catchable

The flat C wrappers are **not uniformly exception-safe**. Structural calls
(`msgcore_mgr_create`, `msgcore_field_child`, `msgcore_mgr_save`, the trigger
calls, `msgcore_field_get_int_any`) wrap their bodies in `try`/`catch` and report
failure as `NULL`/`0`. The typed scalar accessors do not:

```cpp
MSGCORE_C_API int
msgcore_field_get_int(MsgFieldHandle hField)
{
    if (!hField) return 0;
    return toField(hField)->c_int();     // throws P2Pevent* out of the DLL
}
```

Nor can a C++ host reliably catch that. `Msgcore_c.h` is `extern "C"`, and under
MSVC's `/EHsc` model an `extern "C"` function is *assumed not to throw* — so the
optimiser may delete the handler. An earlier revision of `MgrCApiTest` caught
this throw in Debug and did **not** catch it in Release, in the same source file.

The rule is therefore stricter than "handle the error": **ask
`msgcore_field_get_data_type` before you read, or use `msgcore_field_get_int_any`.**
`MgrCApiTest` §2 shows both.

### 5. `P3PmsgData` had no copy constructor — FIXED

The most consequential of the lot. `P3PmsgData` owns its cell through a raw
`m_pObject` pointer and deletes it in the destructor, but declared no copy
constructor — so the compiler generated a shallow one and two objects ended up
owning the same cell. The source read back as `0` as soon as the copy died, and
the second destructor faulted.

Every sibling class already declared one: `P3PmsgField`, `P3PmsgList`,
`P3PmsgVect`, `P3PmsgBSTR`, `P2PmsgRecurs`. `P3PmsgData` was the omission, and
it stayed invisible because the derived classes are what callers normally copy.
`P3PmsgTime`'s copy constructor delegates straight into it, which is how
[`RecursTimeTest`](RecursTimeTest) surfaced it.

### 6. The scalar size ladder omitted five types — FIXED

`VBLockData_Sizeof_uv` handled INT08–UINT64 and DOUBLE, then fell through to
`ASSERT(0)` — leaving out **FLOAT, TIME32, TIME64, BOOL and WCHAR**, every one
of which `VBLockData_Copy` already knew how to move. Assignment sized the copy
from that fall-through, so the value silently failed to arrive.

### 7. `P3PmsgTime` could not read back what it wrote — FIXED

It sets `VBLockData_TIME64`; `c_time64()` demanded `VBLockData_INT64` and threw
*"Incompatible c_time64() type (Time64)"*. `c_time64` is the reader for both
64-bit tags — there is no `c_int64` — and now accepts both. Alongside: the
value constructor flagged its cell NULL, and `ToString()` printed the minutes
twice, in the seconds field.

### 8. 64-bit heaps could not be built through `P3PmsgBSTR` — FIXED

The `VBHeapRoot_*` control-key accessors are `Addr32`/`16`/`08` ladders and the
`Addr64` case had been added to only three of fourteen; the rest fell through to
`ASSERT(0)` and an *"Internal VBHeapRoot.uVBLockAddr corruption"* throw. Eleven
missing branches added. `P2PmsgMgr(VBLock_Addr64, …)` — what every other harness
uses — happens not to reach the missing ones, which is why this survived.

### 9. Three more declared-but-never-defined members

The same shape as `GetPrev` in finding 2: visible in a header, an unresolved
external at **link** time for anyone who calls them.

* `P3PmsgData::c_time()` — now implemented, symmetric with `c_time64`.
* `P3PmsgBSTR::IsDirty()` — now implemented, delegating to the heap like
  `Sizeof()` does.
* `P3PmsgBSTR::IsFragmented()` — **still undefined**. Unlike dirtiness there is
  no `P2PmsgHeap` primitive behind it, so implementing it would mean inventing a
  fragmentation policy rather than wiring an existing one. Do not call it.

And one that was not a link error but could not compile at all:
`P2PmsgMgrnn`'s default constructor called a two-argument `P2PmsgMgr(uBSTRnn,
2024)` that does not exist, so `P2PmsgMgr32 oMgr32;` — the usage its own header
comment advertises — was ill-formed. Being a template, nothing had instantiated
it. Fixed.

### Smaller notes

* `P2PmsgMgr::Sizeof()` is the heap's **current allocation**, not bytes in use.
  It starts at `nSizeInitial` and only moves when the tree outgrows it — a small
  tree in a 4 KB heap reports 4 KB before and after.
* `RootPath2Object` reports a missing path by **throwing** a `P2Pevent`
  ("Path to object does not exist"), not by returning an empty object. A lookup
  service must convert that into an answer — and must do so *inside* the handler,
  because an exception escaping a hub handler makes the pump drop the connection
  (`P2Pwin32.cpp:3279`).
* `P2Pevent::GetModule`/`GetMessage`/`GetGroup`/`GetAdvice` return `CString`, not
  `LPCTSTR`. Compare or format them through an explicit `(LPCWSTR)` view or the
  overload resolution is ambiguous against `CStringT`'s operators.
* A `P2PeerMsg` payload is capped at `MAX_P2Psize = 32768` bytes
  (`P2PeerMsg.h:36`). A store larger than that needs chunking.
* `BEGIN_P2PeerMsg_MAP` imports the **data** symbol `P2PeerHub::P2PeerMsgMap`,
  and the linker refuses `/DELAYLOAD` on a DLL an image imports data from
  (`LNK1194`). `WsaQueryTest` therefore does not delay-load `Targetcore.dll` —
  the same reason `_Targetcore_UseExamples\DirectExamples\PipeMsgMapTest` does not.

---

## The sibling dependencies

**A clone of this repository alone does not build.** These are examples *of* a
library that is not vendored here, and the project files reach outside the
repository in four distinct ways. All four are relative paths that assume this
tree is checked out inside the parent MSCS solution — they are not submodules,
and there is no fallback.

| # | What is reached | Where from | Needed by |
| - | --------------- | ---------- | --------- |
| 1 | `..\..\..\Msgcore` and `..\..\..\Targetcore` | headers, at compile time | all eight |
| 2 | `..\..\..\lib\$(Platform)\$(Configuration)\{Msgcore,Targetcore}.lib` | import libraries, at link time | all eight (`Targetcore.lib` for 5 and 6) |
| 3 | `..\..\..\bin\$(Configuration)64\{Msgcore,Targetcore}.dll` | staged by a post-build `xcopy`, at run time | all eight |
| 4 | `..\..\..\vsutils\DelayLoadReport.cpp` | compiled in, to report a `/DELAYLOAD` fault legibly | `WsaStoreTest`, `WsaQueryTest` |

### What that means for CI

Stated plainly, because a green tick that verified nothing is worse than no tick:

* [`ci.yml`](../.github/workflows/ci.yml) runs on every push and **compiles nothing**.
  It checks what this repository can check about itself — that the solution and the
  project files agree, that every source they name exists, that the four sibling
  bindings above are still exactly four, and that the shipped Markdown does not
  link to files that are gone.
* [`solution-build.yml`](../.github/workflows/solution-build.yml) is the one that
  really builds and really runs the eight, and it is **`workflow_dispatch`-only**
  because it needs the siblings supplied to it. If they are not, it **fails** — it
  does not print "skipped" and exit `0`.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for the
full text.
