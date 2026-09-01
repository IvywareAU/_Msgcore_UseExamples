# `FacadeExamples` — the **Msgcore** examples, on the facade

Every harness from [`DirectExamples`](../DirectExamples) rewritten
on **MsgFacade** instead of on Msgcore's C++ classes.

Same eight subjects, same section order, same exit-code contract, so the two
trees read side by side. What changes is everything underneath: no MFC, no
`CWinApp`, no `stdafx.h`, no kernel headers, no `P2Pevent`, and no assert trap
— because there is no MFC in these processes to assert.

```
  the harness ─────> MsgFacade.dll ────> Msgcore.dll
                       (flat vtable ABI,   (MFC classes, a packed
                        HRESULT, no macros) offset-addressed heap)

  the two mesh harnesses also ─> TargetFacade.dll ──> TargetCore.dll
```

## Status

| | |
|---|---|
| build | `Debug\|x64` and `Release\|x64`, **0 errors, 0 warnings** at `/W4` |
| run | **8/8 PASS in both configurations** |
| checks | **654 per configuration** |

```powershell
.\run_all.ps1                  # build + run Debug
.\run_all.ps1 -Config Release
.\run_all.ps1 -NoBuild
```

Every harness reports by process **exit code** — `0` success, `1` setup
failure, `3` a failed check or undelivered traffic — and prints
`N checks, M failed` as its last line. Exit code `2` was "an MFC/CRT assertion
fired" in the originals; it is unreachable here by construction.

## The eight

| harness | subject | checks |
|---|---|---|
| `DataFieldTest` | the data model bottom-up: typed values, names, descendants, attributes, the node value stack, and how failure is reported | 134 |
| `ListVectTest` | the containers: a list, a vect, the 32-slot seam, containment, a cursor | 103 |
| `MgrPersistTest` | the store: heap, save/load, positions and paths, change notification | 68 |
| `MgrCApiTest` | the same store through the **raw flat ABI** — `MsgFacade.h` only, no sugar | 158 |
| `RecursTimeTest` | the walker (push / pop / break) and a node's timestamp | 69 |
| `BstrWidthTest` | the heap under a store, its addressing width, its ceilings, and demand paging | 85 |
| `WsaStoreTest` | **both facades**: a whole store shipped between two hubs over loopback TCP | 15 |
| `WsaQueryTest` | **both facades**: the store stays put; typed answers cross the wire | 22 |

## What one project file needs

`common\Light.props` is the whole build configuration, and its size is the
point:

* one include directory (`..\..\MsgFacade\include`),
* one import library (`MsgFacade.lib`),
* the DLLs staged beside the exe.

The two mesh harnesses set `LightNeedsMesh=true` in their `Globals` group,
which adds the second facade and nothing else. No `<UseOfMfc>`, no precompiled
header, no `stdafx.cpp`. Each `.vcxproj` is 45 lines and names one `.cpp`.

## Where the two trees differ

Six of the eight are clean translations. These are the places where a
faithful translation was not available, each also flagged at the point in the
source where it comes up.

**TWO OF THEM CLOSED.** This list was written against **ABI 1**, and it argued
for two absences: the per-node value stack and demand paging. ABI 2 exposes
both, so entries 2 and 6 now record what the argument was and what replaced it —
which is more useful than deleting them, because the argument for 2 is still
right about the ordinary case.

1. **`DataFieldTest` has no loose value cells.** The original built
   `P3PmsgData` objects on the stack — a cell owning its own little heap,
   belonging to no tree. The facade has no such thing on purpose: everything it
   can name lives in a store. Section 1 declares its cells into a scratch store
   instead.
2. **`DataFieldTest` §5 is a scoped override BOTH ways — *was* "not `MsgStck`".**
   The argument for leaving the kernel's per-field value stack out was that its
   whole purpose was holding a value across a mutation without holding a handle,
   which is what a path-based node makes safe — so the section was a plain
   client-side save/restore, shown surviving 200 heap growths. That half is
   still there and still right for the ordinary case. **ABI 2 exposes the slot**
   for the two things the hand-rolled version cannot do: the NAME travels with
   the value, and the pair lives in the STORE, so any reference to that node can
   pop what another one pushed. It NESTS, which every tier here documented the
   opposite of until this section pushed twice — see defect 7 below.
3. **`ListVectTest` has no backward list walk and no vect `InsertAt`.** The
   original walked a list by `VBLaddr`, the opaque heap address of a cell:
   exactly the thing a relocation invalidates. Indexing replaces it (and is
   `O(index)`, so a full scan is `O(n²)` — the header says so). The 32-slot seam
   is crossed with the two verbs that do exist, a past-the-seam declare and a
   delete that shifts across it.
4. **`RecursTimeTest` §3 reads through paths, not through the walker.** The
   original's `P2PmsgRecurs` forwarded `r_data()` to the deepest cursor. This
   walker reports *where it is* — kind, name, depth — and the section builds
   each stop's path from the depth, which is nine lines once and yields paths
   that outlive the walk.
5. **`RecursTimeTest` §4 is a node's timestamp, not `P3PmsgTime`.** The kernel's
   TIME64 *cell* is not exposed; a node's *stamp* is. So any node can carry one
   whatever its value type — and the cost is that a timestamp cannot be a
   value's type. Store it as `INT64` and put the units in an attribute.
6. **`BstrWidthTest` loses one whole section, and got the other back.**
   `P3PmsgBSTR`'s flag-addressed sections (`VBLockBSTR_MSG` and friends) become
   reserved child NAMES — which are addressable from outside, as a flag is not.
   Demand paging (`PageRegistration` / `SafeRegistrationPush`) used to be listed
   beside it as having no facade spelling **at all**, on the grounds that a
   paging callback fires from inside the kernel with the store's lock held and
   the heap mid-operation — the one moment the facade's own contract has nothing
   useful to say. **ABI 2 exposes it** and resolves that by *stating* the
   contract rather than softening it: §6 carries the table, and every line of it
   is the inverse of a change sink. Two defects came out of writing that section
   (8 and 9 below).
7. **`MgrCApiTest` is about a different flat ABI.** The original is Msgcore
   through `Msgcore_c.h`. This one is the same subject through MsgFacade's
   vtable ABI used raw — no `MsgFacadeFn.hpp`, no RAII, no `std::wstring` —
   because that is what a language binding sees. Its first rule survives
   ("every handle you are given, you must destroy"); its second, LIVE vs
   DETACHED, is gone, and §1 is the proof.

## Nine things this tree measured

The originals' README records nine Msgcore defects found by writing them, and
the lesson that "every one lived on a code path nothing in the repo had ever
executed". Writing these found nine more, four of them in MsgFacade — which had
419 green smoke-test checks and still had not had these calls made. The last
three arrived with the ABI 2 sections, and are the same lesson again: the calls
existed, and nothing had made them.

1. **`MAX_NAME` was 127 and the real bound is 63** — *fixed in MsgFacade*. The
   facade trusted `P3Pmsg_IsValidItemname`, which counts to 127;
   `P3PmsgName` stores 63 UTF-16 units inline. A name in between passed every
   check, reached the core, and died in an MFC `ASSERT` — which `AfxAbort`
   turns into a process exit, not a return. `DataFieldTest` §6 now pins both
   ends, in units and in astral code points.
2. **`IMsgStore::Rename` renamed the FILE** — *fixed in MsgFacade*.
   `P2PmsgMgr::Rename` is named for `m_strFilename`: it `MoveFileEx`s the store
   to the string it is given. So the facade's "rename the root" answered
   `MSGF_E_CORE` for an in-memory store and would have silently MOVED the file
   of a saved one. `GetRootname` is `r_name().c_name()`, so its inverse is the
   one now called. `BstrWidthTest` §1 is the first caller in this repo ever to
   ask.
3. **A tiny `initialBytes` hangs the core** — *worked around in MsgFacade*. A
   `CreateStore` initial request of 1..256 bytes does not fail: it does not
   return, at any of the three widths. 512 is the smallest that comes back, so
   a non-zero request below it is now raised to it.
4. **The addressing width is invisible from outside a store.** The original
   checked `Sizeof()` rising with the width across three `P3PmsgBSTR`s and it
   held; across three `P2PmsgMgr`s it did not, and the original noted that
   without drawing the conclusion. `size()` is *committed* bytes, and a manager
   commits its initial request up front — which swamps a header difference of a
   few bytes.
5. **A container has no attribute collection.** Declaring an attribute on a
   list or a vect is `MSGF_E_TYPE`, and so is *counting* its attributes — the
   same reason the kernel's own `SelectItem` raises on both. `ListVectTest` §5
   walks a mixed collection and attributes only what can be attributed.
6. **A brand-new store is already dirty.** `IsDirty` means "there are bytes here
   that no file has", and building the empty heap and its root made some. It is
   the answer to "would a save write something", not to "have I changed
   anything".
7. **The node value stack NESTS, and an assertion said it could not** — *fixed
   in Msgcore*. `MsgStck__AllocItem` asserted its saved slot was empty, so a
   second push tripped an assertion in a debug build; every tier of this
   repository then documented "pushing twice replaces the first", written from
   the assertion. `MsgStck::Push` three functions above it reads the current
   head, allocates, and re-links the old head onto the new item — a linked
   stack. Measured in Release, where nothing asserts: push, push, pop, pop
   unwinds correctly. The assertion is gone; the documentation is corrected in
   `MsgFacade.h`, in the `.idl`, and here.
8. **`PageRegistrationPush` saves the registration and does not suspend it** —
   *handled in MsgFacade*. It copies the live callbacks into the saved slots and
   leaves the live ones installed, so the `SafeRegistrationPush` bracket saves
   and restores the same values and suspends nothing. Defensible for a caller
   who means "save what is installed, put mine in, give me the old one back";
   not what SUSPEND means, and suspend is what the one documented use needs — a
   Save walks the whole store and must not fault all of it in on the way past.
   `IMsgStore::PushPaging` clears the registration after saving it. Nothing else
   in the kernel calls Push, so there was no caller to surprise.
9. **A retype to a variable-length type then a write tripped a heap assertion**
   — *fixed in Msgcore*. `P2PmsgObject_NewVBLockData` asserted that the INLINE
   data lump could hold the type's full logical payload, at the moment it is
   about to become a CHAINED one — which needs only a header and a chain
   pointer. A child declared `INT32` gets 6 bytes of inline data space; retyping
   it to `WSTR16` makes its type-computed size 8; 8 does not fit in 6, so the
   cell chains, which is correct and reads back intact. 6 is also exactly
   `VBLockData_Sizeof_Min` on a 32-bit heap, so the chained form fits and the
   check now asks for that. Measured with an instrumented build before it was
   changed: `req=8 claim=8 alloc=6`, against `req=60 claim=10 alloc=10` for the
   same growth on a cell declared as text in the first place — which is why only
   the retype path ever tripped it.

## Layout

```
FacadeExamples/
  common/
    Light.props        every build setting, for all eight
    LightHarness.h     CHECK / CHECK_HR / Section / TempPath / HrName
    LightMesh.h        the extra bits the two mesh harnesses need
  DataFieldTest/  ListVectTest/  MgrPersistTest/  MgrCApiTest/
  RecursTimeTest/ BstrWidthTest/ WsaStoreTest/    WsaQueryTest/
  FacadeExamples(2022).sln
  run_all.ps1
```

## Related

* [`../../MsgFacade`](../../MsgFacade) — the facade these are written against
* [`../DirectExamples`](../DirectExamples) — the originals
* [`../../_TargetCore_UseExamples/FacadeExamples`](../../_TargetCore_UseExamples/FacadeExamples) — the
  same exercise for the messaging kernel, over `TargetFacade`
* [`../ComExamples`](../ComExamples) — the same eight
  subjects through `MsgcoreCom`, the ATL layer
