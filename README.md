# `_Msgcore_UseExamples`

Worked examples for **Msgcore** — the MSCS message *content* library: a
self-describing tree of named, typed cells packed into a relocation-safe heap
that can be saved, addressed by position, and shipped.

This repository holds **four trees**. They are not four different subjects —
they are the **same eight subjects, four times over**, once for each way a
caller can reach the library. That is the whole point of the layout: put the
four side by side and the difference you are looking at is the *binding*, never
the material.

| Tree | Language | Reaches Msgcore through | Built by |
| --- | --- | --- | --- |
| [`DirectExamples`](DirectExamples) | C++ | `Msgcore.lib` and MFC — the C++ classes themselves | `DirectExamples(2026).sln` |
| [`FacadeExamples`](FacadeExamples) | C++ | `MsgFacade.dll`, a macro-free flat-vtable facade | `FacadeExamples(2026).sln` |
| [`ComExamples`](ComExamples) | C++ (+ PowerShell) | `MsgcoreCom`, an ATL dual-interface COM server over the facade | `ComExamples(2026).sln` |
| [`dotNetExamples`](dotNetExamples) | C# | `MsgcoreCom`, by vtable and late-bound | `build.ps1` (Roslyn `csc`) |

There is **no solution at this root, by design.** Each tree builds on its own —
different toolchain, different prerequisites, different failure modes — and a
solution spanning all four would claim a build relationship that does not exist.
Start in the tree you care about; its own README is the documentation.

---

## The eight subjects

The same list in all four trees, in this order. Each builds on the one before.

| # | Harness | Subject |
| - | ------- | ------- |
| 1 | `DataFieldTest` | `P3PmsgData` typed cells, `P3PmsgField` = name + data, attributes, descendants, the value stack |
| 2 | `ListVectTest` | `P3PmsgList` and `P3PmsgVect`, the 32-element spill seam, nesting, generic traversal |
| 3 | `MgrPersistTest` | `P2PmsgMgr`: one heap per tree, `Save`/`Load`, `P2Pos` addressing, path resolution, change triggers |
| 4 | `MgrCApiTest` | the same store through the flat API — live vs detached handles, and handle ownership |
| 5 | `WsaStoreTest` | a whole store serialised, sent between two hubs over loopback TCP, and rebuilt on the far side |
| 6 | `WsaQueryTest` | the store stays put and is *queried* across the mesh, request/response |
| 7 | `RecursTimeTest` | the recursive subtree walker, and the TIME64 cell |
| 8 | `BstrWidthTest` | the heap under everything — its addressing width, and the paging callbacks |

Every harness in every tree keeps the same exit-code contract, so one runner
reads the same in all four:

| Code | Meaning |
| ---- | ------- |
| `0` | success — every check passed |
| `1` | setup failure (startup / factory) |
| `2` | an assertion fired |
| `3` | a check failed, or nothing was delivered before the timeout |

---

## What each tree is actually for

### [`DirectExamples`](DirectExamples) — the library as its author wrote it

Eight MFC-dynamic console harnesses written straight against the exported C++
classes (`P3PmsgData`, `P3PmsgField`, `P3PmsgList`, `P3PmsgVect`, `P2PmsgMgr`)
and against the flat C ABI in `Msgcore_c.h`. They link `Msgcore.lib`, and the
two networked ones link `TargetCore.lib` as well.

This is the reference tree: the other three are measured against it, and it is
the only one that documents each harness in **its own README** — eight of them,
linked from the tree README. Read it first.

### [`FacadeExamples`](FacadeExamples) — the same thing without the macros

Every harness rewritten on **MsgFacade**, the flat-vtable facade DLL over
Msgcore's object model. Same eight subjects, same section order, same exit
codes, so the two trees read side by side — and everything underneath changes:
no MFC, no `CWinApp`, no `stdafx.h`, no kernel headers, no assert trap.

What it exists to show is that the facade's ABI 2 (paging, and the node value
stack) really does cover the model, and what it costs to say the same thing
through `HRESULT`s instead of exceptions.

### [`ComExamples`](ComExamples) — the same thing from outside the process

Eight harnesses that reach the model through **COM interfaces** on
`MsgcoreCom`, the ATL dual-interface server that sits over the facade, plus a
late-bound **PowerShell** client under `script\`.

The distinguishing property, and the reason this tree is worth its weight: these
executables link **nothing of MSCS**. Not `Msgcore.lib`, not MFC, not ATL —
only `ole32`, `oleaut32`, `uuid` and a generated type-library header. If they
pass, the COM surface is genuinely self-contained.

### [`dotNetExamples`](dotNetExamples) — the same thing from a managed runtime

The same eight subjects in **C#**, over the same COM server, both early-bound by
vtable and late-bound through `IDispatch`. Built by Roslyn `csc` out of
[`build.ps1`](dotNetExamples/build.ps1) — there is no `.csproj` and no MSBuild
in this tree.

It is where the managed/native seam gets measured: what the CLR does to an
`HRESULT`, what a COM-callable wrapper does to apartment marshalling, and which
of those are properties of .NET rather than of the server.

---

## Building

Each tree builds independently and documents its own prerequisites. In outline:

```powershell
cd DirectExamples ; .\run_all.ps1                  # build + run Debug
cd FacadeExamples ; .\run_all.ps1 -Config Release
cd ComExamples    ; .\run_all.ps1 -IncludeScripts  # also the PowerShell client
cd dotNetExamples ; .\run_all.ps1
```

Each `run_all.ps1` builds its tree, runs all eight harnesses, prints a pass/fail
table, and **exits with the number of failures**.

### The sibling dependencies

None of these trees builds standalone, and that is a property of the material
rather than an oversight. Every path below is **relative**, resolved from a
project file at `<repo>/<Tree>/<Harness>/`, and assumes this repository is
checked out inside the parent MSCS solution as `MSCS\_Msgcore_UseExamples`:

| | Reached | Wanted by |
| - | ------- | --------- |
| 1 | `..\..\..\Msgcore`, `..\..\..\TargetCore` | headers, at compile time — `DirectExamples` |
| 2 | `..\..\..\lib\$(Platform)\$(Configuration)\*.lib` | import libraries, at link time — `DirectExamples` |
| 3 | `..\..\..\bin\$(Configuration)64\*.dll` | staged by a post-build `xcopy`, at run time |
| 4 | `..\..\..\vsutils\DelayLoadReport.cpp` | compiled in by the two networked `DirectExamples` harnesses |
| 5 | `..\..\..\MsgFacade`, `..\..\..\TargetFacade` | the facade and the two COM servers — the other three trees |

**Three leading `..\` and not two.** Each tree used to be a repository of its
own, sitting directly under `MSCS\`; combining them put every tree one directory
deeper. `.github/ci/check_repo_invariants.py` pins these paths per tree for
exactly that reason: a level lost in a move fails there, on a runner with no
compiler, in seconds — instead of surfacing as `LNK1181` on somebody's machine.

`vsutils\` is **not published anywhere**, and `Msgcore`, `MsgFacade`,
`TargetCore` and `TargetFacade` are private repositories. See
[`CONTRIBUTING.md`](CONTRIBUTING.md) and the two workflows for what that means
for CI.

---

## Continuous integration

Stated plainly, because a green tick that verified nothing is worse than no tick
at all:

* **`ci.yml`** runs on every push and **compiles nothing.** It runs
  `.github/ci/check_repo_invariants.py`, which checks bookkeeping only:
  solution/project parity for both configurations in all three MSBuild trees,
  that every source named exists, that the pinned outward paths and the paths
  built in the `.props` files are unchanged, and that the shipped Markdown does
  not link to files that are gone.
* **`solution-build.yml`** is the one that really builds and runs, and it is
  `workflow_dispatch`-only because it needs sibling checkouts that cannot be
  supplied automatically.

---

## The other family

The sibling repository [`_TargetCore_UseExamples`](../_TargetCore_UseExamples)
is laid out the same way and covers the other half: **moving messages between
hubs** rather than what is in one. Where a question here is about the mesh —
hubs vs pumps, thread affinity, the login handshake — it is answered there, in
[`ArchitectureFAQ.md`](../_TargetCore_UseExamples/ArchitectureFAQ.md),
and is not re-explained in this repository.

---

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.
Licensed under the Apache License, Version 2.0. See [`LICENSE`](LICENSE) for the
full text.
