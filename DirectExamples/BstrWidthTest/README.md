# BstrWidthTest — the heap, its address width, and paging

The last harness. Follows [`RecursTimeTest`](../RecursTimeTest); no networking.

Everything up to here has stood on a heap without naming it. This is that heap:
`P3PmsgBSTR`, the class both `P2PmsgMgr` and TargetCore's `P2PeerMsg` are built
from, and the **addressing width** that decides how big every internal offset
is.

## What it covers

| § | Subject |
| - | ------- |
| 1 | `P3PmsgBSTR` — a heap, a named root item, and the well-known sections |
| 2 | `P3PmsgBSTR16` / `32` / `64` — pinning the width, and what it costs |
| 3 | `P2PmsgMgr16` / `32` / `64` — the same tree over three heaps |
| 4 | `P3PmsgField16` |
| 5 | `PageRegistration`, `PageDatasetIn`/`Out`, and `SafeRegistrationPush` |

## An envelope, not just a heap

`P3PmsgBSTR` is a heap plus a root `P3PmsgItem` plus a fixed set of **sections**
addressed by flag rather than by name:

```cpp
P3PmsgBSTR oBSTR(VBLock_Addr32, 4096);
oBSTR.Init(L"Envelope", P3PmsgData(L"payload"));

oBSTR.r_item(VBLockBSTR_MSG, /*bCreate*/ true);   // application payload
oBSTR.r_item(VBLockBSTR_SYS, true);               // system
oBSTR.Exists(VBLockBSTR_MSG);                     // true
```

`VBLockBSTR_ROOT`, `_NET`, `_SYS`, `_EVT`, `_WRP`, `_MSG` are reserved child
items with reserved names. That is how `P2PeerMsg` keeps its routing headers
(`Net`) apart from its application payload (`Msg`) in one object — and it is why
`P2PeerMsg` derives from this class rather than from `P2PmsgMgr`.

A section is an ordinary item, so it takes descendants like anything else.

`r_name()` and `r_data()` are the root seen as its two halves. Watch the
accessor: on a `P3PmsgName` it is `c_name()`; `c_wstr()` is `P3PmsgData`'s and
reads the **value**. A `P3PmsgField` has both, so the distinction only bites
when you hold one half on its own — or when you call `c_wstr()` on a field whose
value is an int and get *"Incompatible c_wstr() type (int32)"*.

## The width is not a label

Every control key in the heap's root header is `uAddrNN` bits wide, so an
*empty* heap already costs more at 64 bits than at 16:

```
P3PmsgBSTR16  154 bytes
P3PmsgBSTR32  168
P3PmsgBSTR64  196
```

That is the trade: a narrower heap is smaller on the wire and caps out sooner.
`P3PmsgBSTRnn<>` and `P2PmsgMgrnn<>` exist so the width is said once, in the
type.

`Sizeof()` reports what is **committed**, not what was requested — the same
distinction [`MgrPersistTest`](../MgrPersistTest) makes for `P2PmsgMgr`. Asking
for a 16 KB heap does not make the number bigger until the tree needs the room,
which is why §3's three managers all report the same 2032 bytes for the same
small tree.

## Paging, and what `SafeRegistrationPush` actually does

A manager can hand "this subtree is not in memory yet" back to the application:

```cpp
oMgr.PageRegistration((PINT_PTR)nKey, &OnPageIn, &OnPageOut);
oMgr.PageDatasetIn(pos);      // calls OnPageIn(nKey, pos)
```

The key is whatever the application wants back — usually a `this` pointer. It
must be **non-zero**: the manager uses it as the "is anything registered" test,
and with nothing registered `PageDatasetIn`/`Out` are inert rather than a crash.

`SafeRegistrationPush` is an RAII bracket around **swapping** that registration.
Its constructor saves the installed callbacks and its destructor restores them:

```cpp
{
    SafeRegistrationPush oPushed(&oMgr);
    oMgr.PageRegistration((PINT_PTR)nKey, &OnAltPageIn, &OnPageOut);
    ...                                   // the alternate pair is live
}                                         // originals are back
```

It does **not** suspend paging in between — the live callbacks stay installed
until something replaces them. It is a save/restore, not a disable. Note also
that `PageRegistrationPush` asserts that nothing is already pushed, so the
bracket does not nest, and `PageRegistrationPop` asserts that all three saved
slots are set — so pushing without a prior registration trips it.

## Two Msgcore defects had to be fixed to make this harness run

1. **64-bit heaps could not be created through `P3PmsgBSTR`.** The
   `VBHeapRoot_*` control-key accessors are `Addr32`/`16`/`08` ladders, and the
   `Addr64` case had been added to only three of fourteen. The rest fell through
   to `ASSERT(0)` and an *"Internal VBHeapRoot.uVBLockAddr corruption"* throw,
   so `P3PmsgBSTR64` asserted on construction. Eleven missing branches added.
   (`P2PmsgMgr(VBLock_Addr64, …)`, which every other harness uses, happens not
   to reach the missing ones — which is why this went unnoticed.)
2. **`P2PmsgMgrnn`'s default constructor did not compile.** It called a
   two-argument `P2PmsgMgr(uBSTRnn, 2024)` that does not exist, so
   `P2PmsgMgr32 oMgr32;` — the usage its own header comment advertises — was
   ill-formed. Being a template, nothing had ever instantiated it.

Also fixed: `P3PmsgBSTR::IsDirty()` was declared next to `Sizeof()` but never
defined (an unresolved external, the third of that shape in this tree). It is
now the same one-line delegation to the heap that `Sizeof()` is.
`IsFragmented()`, declared beside it, is **still** undefined — unlike dirtiness
there is no `P2PmsgHeap` primitive behind it, so supplying one would be
inventing a policy rather than wiring an existing one. Do not call it.

## Build and run

```powershell
& $msbuild ".\BstrWidthTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\BstrWidthTest.exe
```

Exit `0` success · `2` assert · `3` a check failed. Currently **50 checks, 0
failed**, Debug and Release.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for the
full text.
