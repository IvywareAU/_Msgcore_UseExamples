# DataFieldTest — the Msgcore data model, from the bottom up

The first harness in the tree. No networking, no TargetCore: it links
`Msgcore.lib` alone and touches nothing else.

Msgcore is the message **content** library. TargetCore moves trees between
hubs; everything about what a message *contains* lives here.

## What it covers

| § | Subject |
| - | ------- |
| 1 | `P3PmsgData` — typed value cells: `int`, `short`, `INT64`, `double`, `bool`, wide string, blob; `DataType()`, `ToString()`, `ToStringType()` |
| 2 | `P3PmsgField` — name + data; establishing a data cell; rename-vs-retype; deep copy |
| 3 | Descendants — `DeclareItem` / `SelectItem` / `Exists` / `Delete` / `Truncate`, nesting, `bUpdate` |
| 4 | Attributes — `r_Attr(AttrCMD_Create)`, the parallel tree that TreeFs surfaces as `.attr/` and xattrs |
| 5 | `MsgStck` — `Push()` / mutate / `Pop()`, a scoped override held inside the field |
| 6 | `P2Pevent` — the fluent error channel, and recovering from a real throw |

## Two things worth carrying forward

**A name-only field has no data cell.** `P3PmsgField f(L"x")` is a name;
`c_int()` on it has nothing to write into. Assign a `P3PmsgData` first — that is
what establishes the cell, and what makes `DataType()` change:

```cpp
P3PmsgField oField(L"Johnno");
oField = P3PmsgData((int)1);     // establish an INT32 cell
oField.c_int() = 42;             // now legal
oField = P3PmsgName(L"Larry");   // renames; the cell survives
```

Assigning a `P3PmsgName` renames in place. Assigning a `P3PmsgData` replaces the
cell. The overloads look alike and do opposite things.

**Names are bounded at 63 UTF-16 UNITS, and the bound is enforced before the
write.** An overrun throws a `P2Pevent*`, leaving the previous name intact — so
the failure is safe to catch and continue from. Units, not code points: one
astral code point costs two, so 31 emoji fit and 32 do not.

```cpp
try { oName.c_name(std::wstring(64, L'a').c_str(), 0); }
catch (P2Pevent* pEVT) { if (pEVT) pEVT->Cancel(false); }   // false: discard silently
```

`Cancel(true)` reports through the sink and may display on Windows;
`Cancel(false)` discards; `Isolate()` detaches the event so it can be carried
elsewhere. A caught `P2Pevent` **must** be disposed of one of those three ways.

> `P2Pevent::GetMessage()` and friends return `CString`, not `LPCTSTR`. Take an
> explicit `(LPCWSTR)` view before comparing or formatting, or the overload
> resolution goes ambiguous against `CStringT`'s operators.

## Build and run

```powershell
& $msbuild ".\DataFieldTest(2022).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\DataFieldTest.exe
```

Exit `0` success · `2` assert · `3` a check failed. Currently **59 checks, 0
failed**, Debug and Release.

Next: [`ListVectTest`](../ListVectTest) — the containers.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for the
full text.
