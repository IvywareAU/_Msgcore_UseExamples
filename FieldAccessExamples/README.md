# `FieldAccessExamples` — the Msgcore examples, with fields reached by name

The eight harnesses from [`../DirectExamples`](../DirectExamples), redone with the
header-only field-access layer in `Msgcore\MsgFieldRef.hpp`. Each one keeps its
subject, its sections and every check it made. Wherever it writes or reads a
named field, it now uses one of two forms instead of `DeclareItem` and
`SelectItem`:

```cpp
struct Inventory : MsgView                    // a TYPED VIEW, for a fixed schema
{
    MSG_FIELD ( Warehouse, std::wstring );
    MSG_FIELD ( Capacity,  int );
};
MsgViewOf<Inventory> inv ( oMgr );
inv->Capacity = 60000;                        // was DeclareItem(L"Capacity", P3PmsgData(60000), TRUE)
int n = inv->Capacity;                        // was SelectItem(L"Capacity").c_int()

Field ( oMgr, L"Stock" )[L"SKU-1002"] = 12;   // the DYNAMIC form, for run-time names and nesting
```

A typed view member takes only its own type, so `inv->Capacity = L"x"` does not
compile. `Field()` is for names that are known only at run time.
[`../DirectExamples/FieldAccessTest`](../DirectExamples/FieldAccessTest) shows
both forms on their own; this tree shows them doing real work.

Calls with no field-level equivalent stay plain: lists, vects, cursors, the
recursive walker, attributes, value stacks, the BSTR heap, `P2Pos` addressing,
triggers, `Save`/`Load`, and the flat C API. Where it is not obvious why a call
stayed plain, the code says so in a one-line comment. A diff against
`DirectExamples` therefore shows exactly what the operator replaces, and what it
does not.

---

## The eight

| # | Harness | Checks here | Checks in `DirectExamples` | What uses field access |
| - | ------- | ----------: | -------------------------: | ---------------------- |
| 1 | [`DataFieldTest`](DataFieldTest) | 104 | 59 | the Order schema as a view; nesting, run-time names, refusals |
| 2 | [`ListVectTest`](ListVectTest) | 85 | 71 | the telemetry pair as a view; item names read back after a cursor walk |
| 3 | [`MgrPersistTest`](MgrPersistTest) | 67 | 58 | the inventory as a view; stock by `Field()`; `Erase()` still fires the DELETE trigger |
| 4 | [`MgrCApiTest`](MgrCApiTest) | 118 | 75 | the C API unchanged; the C++ side meets it on the same store, in memory and on disk |
| 5 | [`WsaStoreTest`](WsaStoreTest) | 23 | 13 | the store built and, after the wire, read through the same views |
| 6 | [`WsaQueryTest`](WsaQueryTest) | 36 | 18 | the catalogue as views; the query and reply carry named app fields (`P2PeerAppFields.hpp`) |
| 7 | [`RecursTimeTest`](RecursTimeTest) | 78 | 61 | the walked tree built through views; a view on the walker's current node |
| 8 | [`BstrWidthTest`](BstrWidthTest) | 94 | 50 | views and `Field()` on 16-, 32- and 64-bit heaps, with identical results |

All 605 checks pass in both Debug and Release. Each harness has its own README
with a "Field-access port" note.

---

## What porting them showed about `MsgFieldRef.hpp`

The ports found no defects. They found four gaps. The first three are now
closed in the header; the fourth is worked around and commented at the point of
use:

1. **There was no ready-made anchor for a child item — CLOSED.** `MsgViewOf<T>`
   binds to an item you hold, or to a `MsgFieldAnchor`. Binding to
   `oMgr.SelectItem(L"Limits")` does not work, because that is the parent's
   *cursor* item and the next lookup on the parent retargets it. Three harnesses
   each hand-wrote a resolver that found the child afresh by name. The header
   now has `MsgFieldAnchor::Child(parent, L"name")`, which chains for deeper
   levels, and `Field(...)[...].Anchor()`. WsaStoreTest, WsaQueryTest and
   RecursTimeTest use them:

   ```cpp
   MsgViewOf<LimitsView> limits ( MsgFieldAnchor::Child ( oMgr, L"Limits" ) );
   limits->Low = -40;                     // creates Limits on the first write
   ```

   BstrWidthTest keeps its `SectionAnchor`, because a `P3PmsgBSTR` section is
   found by flag, not by name. An anchor protects only *itself*: its lookup
   still moves the parent's cursor, so a `P3PmsgItem&` that someone else holds
   from `SelectItem` moves with it (`MscsUnitTests`, the `Child` cases).
2. **There was no time type — CLOSED.** `MsgTime` is a point in time (seconds
   since 1970 UTC). `Field(x, L"t") = MsgTime(t)` and `MSG_FIELD(when, MsgTime)`
   store the TIME64 cell that `P3PmsgTime` makes, and `AsTime()` reads it back.
   It is its own type, with an explicit constructor, so that a `long long` stays
   an integer. Each reader wants its own type: `AsInt64()` refuses a TIME64
   cell and `AsTime()` refuses an INT64 one. RecursTimeTest's `Created` field
   now goes in as `rec->Created = MsgTime(t)`.
3. **There was no `short` form — CLOSED.** An exact `short` now stores INT16,
   read with `AsShort()` or `MSG_FIELD(n, short)`. Only an exact match does:
   `char`, `unsigned short` and `wchar_t` promote to `int` and still store
   INT32, as they would in any C++ overload set. DataFieldTest checks both.
4. **Attributes are out of reach.** `r_Attr()` returns a `P3PmsgAttr`, which is
   not a `P3PmsgItem`, so there is nothing to anchor a view or a `Field()` on.

Also worth knowing:

* An 8-bit `P3PmsgBSTR` heap is refused by design (`P2PmsgHeap_CheckCreateWidth`),
  so BstrWidthTest checks that refusal rather than putting fields on one.
  `P3PmsgBSTR(VBLock_Addr08, 255)` is also ambiguous at compile time (C2668),
  because `VBLock_Addr08` is the literal `0`; it needs a `(UCHAR)` cast.
* MgrCApiTest's in-memory bridge relies on a C handle being the C++ object
  pointer. Only a comment in `Msgcore_c.h` promises that, so its §4 also does
  the round trip through a saved file, which uses public calls only.

---

## Building and running

The layout is the same as `DirectExamples`: one solution per Visual Studio
generation, and one shared `out\` root.

```
FieldAccessExamples\
  FieldAccessExamples(2026).sln          all eight, VS2026 / v145
  FieldAccessExamples(2022).sln          all eight, VS2022 / v143
  <Harness>\<Harness>(2026|2022).vcxproj
  common\Stage.props                     stages the kernel DLLs beside the exes
  out\x64\{Debug,Release}\               exes + staged DLLs
```

```powershell
.\run_all.ps1                     # build + run Debug
.\run_all.ps1 -Config Release
.\run_all.ps1 -NoBuild
```

**Prerequisite:** `Msgcore.dll` in `..\..\bin\Debug64` / `..\..\bin\Release64`,
plus `Targetcore.dll` for harnesses 5 and 6. Unlike `DirectExamples`, which uses
a per-project `xcopy`, every project stages through one MSBuild `Copy` target in
[`common\Stage.props`](common/Stage.props). All eight stage the same files into
the same `out\` folder, and under `msbuild -m` two `xcopy`s onto one file collide;
`Copy` retries instead. A missing DLL fails the build with a message naming what
to build.

Every harness prints UTF-16 (`_setmode(_O_U16TEXT)`), so from PowerShell read
redirected output with `Get-Content -Encoding Unicode`.

### Exit codes

The same contract as every other tree:

| Code | Meaning |
| ---- | ------- |
| `0` | success — every check passed |
| `1` | setup failure (startup / factory) |
| `2` | an MFC/CRT assertion fired (banner on stderr) |
| `3` | a check failed, or a networked harness timed out |

The `AuthArmOrRefuse` sealing warnings that WsaStoreTest and WsaQueryTest print
on stderr are expected; `DirectExamples` prints them too.

---

## The sibling dependencies

The same as [`DirectExamples`](../DirectExamples/README.md#the-sibling-dependencies):
the projects reach `..\..\..\Msgcore`, `..\..\..\Targetcore`,
`..\..\..\lib\...` and `..\..\..\vsutils\DelayLoadReport.cpp`, and
`common\Stage.props` reaches `..\..\..\bin\$(Configuration)64\`.
`.github/ci/check_repo_invariants.py` pins all of them for this tree.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../LICENSE) for the
full text.
