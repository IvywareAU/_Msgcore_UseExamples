# MgrCApiTest — the same store through the flat C API

The store from [`MgrPersistTest`](../MgrPersistTest), rebuilt against
`Msgcore_c.h`: opaque handles and plain functions, no C++ classes, no operator
overloads. Still no networking.

This is the surface a non-C++ caller binds to — the P2P FileSystem core, a
FUSE/WinFsp daemon, a Python or Rust FFI layer. Every call maps onto something
the C++ harnesses do; the differences are all about ownership and lifetime,
because C has no destructors to lean on.

## What it covers

| § | Subject |
| - | ------- |
| 1 | **Live vs detached** — the contract the whole API turns on |
| 2 | Typed declares and reads; type tags and `msgcore_type_name` / `_from_name`; the two safe ways to read an unknown cell |
| 3 | Navigation with `msgcore_field_child`, enumeration with `msgcore_curs_*` |
| 4 | `P2Pos`, paths, `msgcore_mgr_save` / `_open_file`, and a position surviving the round trip to disk |
| 5 | Structural edits — `rename_child`, `move_child`, `retype_child_*` |
| 6 | The headless trigger sink |

## Rule 1 — destroy every handle you are given

Including the ones that look like they are only telling you something.
`msgcore_field_declare_*` returns a handle to the field it declared; dropping it
leaks.

```cpp
msgcore_field_destroy(msgcore_field_declare_int(hRoot, L"Count", 42, 1));
```

## Rule 2 — live vs detached

```
msgcore_mgr_root      / msgcore_field_child        -> LIVE alias; writes land
msgcore_mgr_as_field  / msgcore_field_select_item  -> DETACHED copy; writes lost
```

Reading is safe through either. Mutating through a detached handle raises no
error, returns success, and **silently discards the change** — reach for the live
pair whenever you intend to write. The difference is one line in the wrapper:
the live calls construct from `r_Object()`, which aliases the heap node; the
detached ones copy the field.

## The sharp edge — a type mismatch is unrecoverable

The wrappers are **not uniformly exception-safe**. Structural calls
(`mgr_create`, `field_child`, `mgr_save`, the trigger calls, `get_int_any`) wrap
their bodies in `try`/`catch` and report failure as `NULL`/`0`. The typed scalar
accessors do not:

```cpp
MSGCORE_C_API int
msgcore_field_get_int(MsgFieldHandle hField)
{
    if (!hField) return 0;
    return toField(hField)->c_int();     // throws P2Pevent* out of the DLL
}
```

**And you cannot reliably catch it.** `Msgcore_c.h` is `extern "C"`, and under
MSVC's `/EHsc` model an `extern "C"` function is *assumed not to throw* — so the
optimiser may delete the handler you wrapped the call in. That is not
hypothetical: an earlier revision of §2 caught this throw in Debug and did **not**
catch it in Release, in this same source file. The throw then sailed past the
`try` to the outermost handler.

So the rule is stricter than "handle the error". Two safe ways to read a cell you
are not certain about, both in §2:

```cpp
// (a) dispatch on the tag
if (msgcore_field_get_data_type(h) == MSGCORE_DATA_INT64)
    v = msgcore_field_get_int64(h);

// (b) the guarded, width-agnostic reader -- reports failure as 0
long long iAny; int bUnsigned;
if (msgcore_field_get_int_any(h, &iAny, &bUnsigned)) { ... }
```

The harness keeps a top-level `catch` anyway, but as a **diagnostic net** — so a
throw that does get through prints something useful instead of aborting silently
— not as a safety mechanism.

## Build and run

```powershell
& $msbuild ".\MgrCApiTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\MgrCApiTest.exe
```

Exit `0` success · `2` assert · `3` a check failed. Currently **75 checks, 0
failed**, Debug and Release.

Next: [`WsaStoreTest`](../WsaStoreTest) — where Msgcore meets Targetcore.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for the
full text.
