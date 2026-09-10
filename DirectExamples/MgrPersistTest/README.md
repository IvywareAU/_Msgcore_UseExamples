# MgrPersistTest — `P2PmsgMgr`: save, address, notify

Follows [`ListVectTest`](../ListVectTest). Still no networking.

The first two harnesses built loose objects, each owning its own little heap. A
`P2PmsgMgr` is what makes a tree a **document**: one heap for the whole graph,
serialisable to a file, addressable by stable position, and able to announce its
own changes.

The manager *is* a `P3PmsgItem` (`P2PmsgMgr.h:163`), so `DeclareItem`,
`SelectItem` and `r_Desc()` work on it directly. It is simply the root node that
owns the heap.

## What it covers

| § | Subject |
| - | ------- |
| 1 | Construction `(uAddrNN, nSizeInitial, nSizeMax)`, `IsValid`, `IsDirty`, `Sizeof`, and the heap growth path |
| 2 | `Save` / `Load`, the file-lock rule, `bDefragment`, save-over-itself, and a mutate-and-resave round |
| 3 | `P2Pos` — `GetP2Pos`, `P2Pos2Field`, `P2Pos2Path`, `GetPath`, `RootPath2Object` |
| 4 | Triggers — `SetTriggerSink`, `CreateTrigger`, `FireTrigger`, `DropTriggers`, and the auto-fire on a real delete |

## Save holds the file open

`Save()` writes the image and then keeps the file open for the manager's
lifetime. The default share mode is `FILE_SHARE_READ` (`P2PmsgMgr.h:302-307`):
other readers may open it, other writers may not. So a writer and a reader of the
same store belong in **separate scopes**:

```cpp
{ P2PmsgMgr oMgr(VBLock_Addr64, 4096, 1u << 20);
  BuildInventory(oMgr);
  oMgr.Save(szPath, /*bDefragment*/ true); }   // file closes here
{ P2PmsgMgr oLoaded(szPath);                   // ctor-from-filename == Load()
  ... }
```

`Save()` with no filename saves over the store's own file. `Save(path, true)`
compacts the heap on the way out — worth it for a store that has seen many
deletes.

## `Sizeof()` is allocation, not occupancy

It reports the heap's **current allocation**, starting at `nSizeInitial` (plus a
header) and only moving when the tree outgrows it. A small tree in a 4 KB heap
reports 4 KB before and after. §1 forces the growth path with a deliberately tiny
512-byte initial heap and 300 nodes to show what movement actually looks like.

## `P2Pos` is the inode

A C++ reference into the tree is good only while the tree holds still. A `P2Pos`
is an offset within the heap image: stable, integral, survivable across
`Save`/`Load`, and storable. It is what the P2P FileSystem hands out as an inode
number. The loop closes in all three directions:

```
reference --GetP2Pos()--> P2Pos --P2Pos2Path()--> "..Stock.SKU-1002"
    ^                                                     |
    +-------------- RootPath2Object() --------------------+
```

The caution: `GetP2Pos()` means something only on a **live** node — one reached
through the manager's own tree, which is what `SelectItem` returns a reference
to. A detached deep copy has a position in its own private heap, addressing
nothing in the manager. (The C API makes that distinction explicit; see
[`MgrCApiTest`](../MgrCApiTest).)

## Triggers without a window

Msgcore's original trigger path posts a Windows message to an `HWND`. A daemon,
a FUSE mount or a console harness has none, so the manager also carries a plain
function-pointer sink (`P2PmsgTriggerSink`, `Msgcore.h:121`) fired alongside the
`HWND` path for every armed node:

```cpp
oMgr.SetTriggerSink(&OnTrigger, &oCapture);
oMgr.CreateTrigger(TRIGGER_UPDATE | TRIGGER_INSERT, (HWND)nullptr, pos);
oMgr.FireTrigger(pos, TRIGGER_UPDATE);        // returns 1 -- one arm matched
```

With `hWnd == NULL` the `PostMessage` leg is a harmless no-op and the sink is the
only delivery. Arming is per node **and** per mask, and the arms are independent
— dropping `UPDATE` leaves `INSERT` live.

> Do not use `TRIGGER_DELETE` as a probe. A DELETE pass is read as "the object is
> gone" and drops **every** registration on that node. A genuine `Delete()`
> through the tree fires it for you, which §4 demonstrates.

## Build and run

```powershell
& $msbuild ".\MgrPersistTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\MgrPersistTest.exe
```

Scratch stores are written under `%TEMP%` and removed afterwards.

Exit `0` success · `2` assert · `3` a check failed. Currently **58 checks, 0
failed**, Debug and Release.

Next: [`MgrCApiTest`](../MgrCApiTest) — the same store in flat C.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for the
full text.
