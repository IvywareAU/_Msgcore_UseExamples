# RecursTimeTest — recursive walking, and the TIME64 cell

Follows [`WsaQueryTest`](../WsaQueryTest). Back to pure Msgcore, no networking.

Two parts of the model the first six harnesses never reach: `P2PmsgRecurs`,
which walks a whole subtree, and `P3PmsgTime`, the only typed subclass of
`P3PmsgData`.

## What it covers

| § | Subject |
| - | ------- |
| 1 | `P2PmsgRecurs` — one flat loop over a whole subtree, and the depth-first order it produces |
| 2 | `Push` / `Pop` / `Break` — the caller drives the descent |
| 3 | `c_wstr` / `r_data` / `IsField` / `IsList` / `IsVect` at each stop |
| 4 | `P3PmsgTime` — the TIME64 tag, `c_time64`, `ToString`, copy and assignment |
| 5 | a TIME64 cell inside a store, and through `Save`/`Load` |

## The walker is not an iterator

`P3PmsgCurs` walks one level. `P2PmsgRecurs` owns a *chain* of cursors, one per
open level, and splices descent into `operator++`. What it does **not** do is
descend by itself — the caller decides, per node, by calling `Push()`:

```cpp
P2PmsgRecurs oRec(oRoot);
while (!oRec.IsEoRecurs())
{
    visit(oRec.c_wstr());
    if (oRec.IsField())        // a list or a vect has no named children
        oRec.Push();           // the NEXT ++ lands on this node's first child
    ++oRec;
}
```

That is the whole traversal, at any depth. Dropping the `Push()` reduces it to
a one-level cursor; making it conditional gives you a pruned walk for free.

**Nothing in that loop pops.** `operator++` finds the deepest open cursor, sees
it is spent, pops it and retries — so coming back up a level, or several,
costs the caller nothing. `Push()` on a node with no descendants is safe for the
same reason: the next `++` immediately unwinds it.

Construction takes the item whose *descendants* are walked, so the root itself
is never visited, and the cursor starts on element 0 — visit first, then
advance.

`Push()` returns the level it entered (counting from 1) and `Pop()` the level it
returned to. `Pop()` always closes the **deepest** open level. `Break()`
abandons the walk wherever it is, at whatever depth.

Push on a list or a vect throws *"Attempt to push non-P2PmsgItem environment"* —
their contents are cells and elements, not named children, so there is nothing
for a cursor chain to stand on.

## `P3PmsgTime` is a tag, not a format

It stores a 64-bit `time_t` — the value `CTime` carries — under
`VBLockData_TIME64`. The bits are identical to an `INT64` holding the same
number; the *tag* is the point. It survives `Save`/`Load`, so a stored timestamp
is still known to be a timestamp, and `ToString()` renders it as
`2026-08-09 14:30:00` rather than as `1786249800`.

```cpp
P3PmsgTime oTime(CTime(2026, 8, 9, 14, 30, 0).GetTime());
oTime.DataType();     // VBLockData_TIME64
oTime.c_time64();     // the value back
oTime.IsNull();       // false -- constructed FROM a value
```

Default construction gives a NULL cell of the right type: the tag says what the
cell is *for*, the NULL attribute says it has no value yet. `ToString()` on it
returns `"Null"`.

`c_time64()` is the reader for **both** 64-bit tags. There is no `c_int64`, so
an `INT64` cell has always been read through here too.

## Four Msgcore defects had to be fixed to make §4 and §5 work

`P3PmsgTime` had never been exercised. In order:

1. **`c_time64()` rejected its own type.** It required `VBLockData_INT64`, so a
   cell `P3PmsgTime` had just written threw *"Incompatible c_time64() type
   (Time64)"*. It now accepts both 64-bit tags.
2. **`P3PmsgData` had no copy constructor.** It owns its cell through a raw
   pointer and deletes it in the destructor, so the compiler-generated shallow
   copy gave two objects one cell: the source read back as `0` once the copy
   died, and the second destructor faulted. Every sibling class —
   `P3PmsgField`, `P3PmsgList`, `P3PmsgVect`, `P3PmsgBSTR` — already declared
   one. This is why `P3PmsgTime oCopy(oTime)` poisoned `oTime`.
3. **The scalar-size ladder omitted TIME64** (and FLOAT, TIME32, BOOL, WCHAR).
   `VBLockData_Sizeof_uv` handled INT/UINT/DOUBLE and fell through to
   `ASSERT(0)` for the rest, so assignment sized the copy from a fall-through
   and the value never arrived — even though `VBLockData_Copy` knew perfectly
   well how to move all of them.
4. **`ToString()` printed the minutes twice**, in the seconds field.

Two smaller ones alongside: `P3PmsgTime(__int64)` flagged its cell NULL even
when constructed *from* a value, and `P3PmsgData::c_time()` was declared but
never defined — an unresolved external, the same shape as `P3PmsgList::GetPrev`.

`MscsUnitTests/MsgcoreSuite.cpp` gained cases for all of it.

## Build and run

```powershell
& $msbuild ".\RecursTimeTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\RecursTimeTest.exe
```

Every loop over a `P2PmsgRecurs` in this harness is step-bounded. A traversal
bug should fail the test, not wedge it — a hung harness blocks the build and
tells you nothing.

Exit `0` success · `2` assert · `3` a check failed. Currently **61 checks, 0
failed**, Debug and Release.

Next: [`BstrWidthTest`](../BstrWidthTest) — the heap underneath all of this.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for the
full text.
