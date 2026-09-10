# ListVectTest — `P3PmsgList`, `P3PmsgVect`, and the cursor

Follows [`DataFieldTest`](../DataFieldTest). Still no networking.

Both containers are themselves `P3PmsgField` subclasses, so a list or a vect has
a name, drops into another field's descendants, and nests inside another
container. That uniformity is the trick: the tree has one node type, and "list"
and "vect" are shapes it can take.

| | `P3PmsgList` | `P3PmsgVect` |
| --- | --- | --- |
| Elements are | `P3PmsgData` — values | `P3PmsgField` — named fields |
| Access | walk by opaque position, either way | random access by `int` |
| Growth | cheap at the tail | `InsertAt`, shifting the rest |
| Storage | linked | 32 inline slots, then continuation blocks |

## What it covers

1. **`P3PmsgList`** — `AddListTail`, `AddListHead`, `+=`, `GetHeadPos`/`GetNext`,
   `GetTailPos`/`GetPrev`, `GetTail`, `DropHead`, `DropTail`, `Truncate`, and a
   heterogeneous list (each cell carries its own type tag).
2. **`P3PmsgVect`** — construction from a prototype cell, `r_data(i)`, `Goto` as
   the non-throwing bounds check, `InsertAt`, `Delete`, `Truncate`, deep-copy
   assignment.
3. **The 32-element seam** — 70 elements spanning the inline `aAlloc[32]` and two
   continuation blocks, then an insert exactly on the boundary.
4. **Nesting** — a vect element that is itself a vect, and a list + a vect + a
   plain item sharing one field's descendants, retrieved by `SelectVect` /
   `SelectList`.
5. **`P3PmsgCurs`** — walking a collection you did not build.

## The traversal idiom

The one Msgcore uses on itself (`MsgAttr.cpp:485`, `MsgDesc.cpp:414`).
`Goto()` doubles as the loop condition — it returns false past the end:

```cpp
P3PmsgCurs& oCurs = oRoot.r_Desc().r_Curs();
for (int i = 0; oCurs.Goto(i); i++)
{
    if      (oCurs.IsList()) { P3PmsgList& oL = oCurs.r_list(); ... }
    else if (oCurs.IsVect()) { P3PmsgVect& oV = oCurs.r_vect(); ... }
    else if (oCurs.IsItem()) { P3PmsgItem& oI = oCurs.r_item(); ... }
}
```

Test the specific shapes **before** the general one: a list and a vect are both
fields, so `IsItem()` would swallow them if it came first. The cursor is owned
by the collection — do not delete it, and do not hold it across a structural
change.

## Two list operations that used to be broken

Both are visible in `MsgList.h`, both failed differently, and both were **fixed
in Msgcore while these examples were being written**. Noted here because any
build of Msgcore older than 2026-08-09 still has them:

**`GetPrev(VBLaddr&)`** — declared at `MsgList.h:83` as the mirror of `GetNext`,
but never defined in `MsgList.cpp`, so calling it was an unresolved external at
**link** time. Now implemented as that mirror: the items were doubly linked all
along (`VBLockItem_GetPrev`), so the backward walk costs nothing extra.

**`AddListHead(data)`** — defined, but it omitted the
`VBLockItem_Init(..., VBLock_Data)` call its twin `AddListTail` makes. The item
went into the list with no type tag, so the next read of it landed on the
`ASSERT(0)` in `VBLockItem_pData` (`P2PmsgVBLock.cpp:4046`). The missing
initialiser is now there, and the two functions differ only in which end they
link to.

`MscsUnitTests/MsgcoreSuite.cpp` gained four cases guarding both.

## Walking a list in either direction

`GetPrev` is an exact mirror of `GetNext` — it **steps back over** the cell it
returns, and the position falls to `0` once the head has been handed out, so the
loop shape is identical bar the starting position:

```cpp
VBLaddr aPos = oList.GetHeadPos();               // forward
while (aPos) { P3PmsgData& o = oList.GetNext(aPos); ... }

VBLaddr aRev = oList.GetTailPos();               // backward
while (aRev) { P3PmsgData& o = oList.GetPrev(aRev); ... }
```

## Build and run

```powershell
& $msbuild ".\ListVectTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\ListVectTest.exe
```

Exit `0` success · `2` assert · `3` a check failed. Currently **71 checks, 0
failed**, Debug and Release.

Next: [`MgrPersistTest`](../MgrPersistTest) — the store.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for the
full text.
