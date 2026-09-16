# `dotNetExamples` — the Msgcore data model from C#

The fourth pass over the same eight subjects.

| tree | language | reaches Msgcore through |
| --- | --- | --- |
| [`DirectExamples`](../DirectExamples) | C++ | `Msgcore.lib`, MFC, the C++ classes |
| [`ComExamples`](../ComExamples) | C++ | `MsgcoreCom`, by vtable |
| **this one** | **C#** | **`MsgcoreCom`, by vtable and late-bound** |
| [`script\ps_client.ps1`](../ComExamples/script/ps_client.ps1) | PowerShell | `MsgcoreCom`, late-bound only |

```powershell
.\run_all.ps1                    # build + run Debug
.\run_all.ps1 -Config Release
.\run_all.ps1 -NoBuild
```

**16/16 green — Debug|x64 and Release|x64, 763 checks per configuration.**

```
DataFieldTestNet     exit=0    175 checks, 0 failed  PASS
ListVectTestNet      exit=0    117 checks, 0 failed  PASS
MgrPersistTestNet    exit=0    102 checks, 0 failed  PASS
MgrCApiTestNet       exit=0     71 checks, 0 failed  PASS
WsaStoreTestNet      exit=0     19 checks, 0 failed  PASS
WsaQueryTestNet      exit=0     28 checks, 0 failed  PASS
RecursTimeTestNet    exit=0    192 checks, 0 failed  PASS
BstrWidthTestNet     exit=0     59 checks, 0 failed  PASS
```

> **THE SERVER MOVED, AND SO DID ITS CONTRACT.** `MsgcoreCom` used to live in
> `Msgcore\com` and sit on `Msgcore_c.h`, the kernel's flat C ABI. It is
> [`MsgFacade\com`](../../MsgFacade/com) now and sits on **MsgFacade**. This tree
> was rewritten against the revised interface, and `common\MsgcoreComInterop.cs`
> was the sharpest part of that: **declaration order is vtable order**, so six
> members leaving `IMsgFieldCom` (`IsVoid`, `IsLive`, `Item`, `IsAttr`, `IsDesc`,
> `Stack`) and four arriving moved every slot below the first change. A
> hand-written `ComImport` interface that disagrees with the server does not
> fail to compile and does not fail to bind — it calls a **different slot**, with
> the arguments of the one you wrote. The `[DispId]` attributes are
> documentation for a late-bound reader and will not save a mismatch.
>
> What changed for a reader of these harnesses is the same list the C++ tree's
> README carries: the path grammar (`""` is the root, `.window.width` a
> grandchild, `.Config.Window@Colour` an attribute, and `FieldAt` takes back what
> `Path` gives out); live-vs-detached gone; the value stack as four members of a
> node, which **nests**; `Nullify`→`Clear` and `RenameFile`→`RenameRoot`;
> `RetypeChild` taking a type; `Child` reaching a container;
> `Declare(update:false)` onto an existing name answering `msgcDeclare`; and
> `IsValidName`, `ChildAt`, walker `Depth`/`Path`, `msgcPath`, `msgcLimit` as new.

The check counts are lower than the C++ COM tree's not because less is covered
but because one C# line covers what several C++ lines had to: a typed read is
`f.Value`, where the C++ harness scores a `Var` construction, a
`VariantChangeType` and a comparison.

---

## What this tree references

`mscorlib`, `System`, `System.Core`. That is the whole list.

No interop assembly, no PIA, no `TlbImp` output, no `Msgcore.lib`, no MIDL
header, no ATL, no MFC, no `ole32`. The nine interfaces are declared by hand in
[`common\MsgcoreComInterop.cs`](common/MsgcoreComInterop.cs) — which **is** the
interop assembly `Add Reference` would have generated, written out in source so
the tree has no generated inputs and no build step beyond `csc`.

**There is no `.csproj`.** `build.ps1` drives Roslyn directly, because every
harness is `common\*.cs` plus one file and there is nothing for a project system
to do. An SDK-style project with `<TargetFramework>net48</>` and these same
sources builds identically.

**x64 is not optional.** `AnyCPU` would load a 32-bit CLR under WOW64 on some
hosts and fail `CoCreateInstance` with a class-not-registered that has nothing
to do with registration.

---

## What the CLR does for free

`ComExamples\common\ComHarness.h` is 1356 lines. This tree's
[`ComHarness.cs`](common/ComHarness.cs) is about a third of that, and the
difference is almost entirely things the marshaller does:

| the C++ harness hand-writes | here |
| --- | --- |
| a `BSTR` wrapper | `string` |
| a `VARIANT` wrapper with eight typed readers | `object`, already the right CLR type |
| a `SAFEARRAY(VT_UI1)` packer and unpacker | `byte[]` |
| a COM smart pointer, and release order | the collector |
| an `IEnumVARIANT` loop | `Msgc.Api.Each`, once |
| a from-scratch `IDispatch` for the event sink | a class implementing an interface |
| `GetErrorInfo` and a `BSTR` to free | `exception.Message` |
| `SystemTimeToVariantTime` for every timestamp | `DateTime` |

---

## The four things it does NOT do for free

These are what the tree exists to measure. Each is checked, not asserted.

### 1. The CLR rewrites some HRESULTs before you see them

Not every failed COM call arrives as a `COMException`. A fixed table of
well-known codes is rewritten into CLR exception types first:

```
msgcStale     0x8004030D -> COMException        (custom, passes through)
msgcNotList   0x80040307 -> COMException
msgcRange     0x8004030A -> COMException
msgcPath      0x80040312 -> COMException
E_INVALIDARG  0x80070057 -> ArgumentException   (REWRITTEN)
E_POINTER     0x80004003 -> NullReferenceException
```

So `catch (COMException)` is correct for Msgcore's own `0x8004030x` range and
**crashes on the argument validation the IDL added for automation clients** —
which is precisely the validation a script is most likely to trip.
`Msgc.Api.Call` catches `Exception` and uses `Marshal.GetHRForException`, which
is right for both. *(DataFieldTest §6.)*

### 2. A managed sink is AGILE, so callbacks run on a thread you never made

This is the finding no other tree in MSCS can make.

A C++ `IDispatch` sink is apartment-bound: `MsgcoreCom` parks it in the Global
Interface Table, re-fetches it on its own MTA dispatch thread, and gets back a
**proxy** that marshals the call into the client's STA. Every C++ COM harness
therefore shows one `tid=` from end to end.

A managed CCW aggregates the free-threaded marshaler. The same GIT re-fetch
hands the dispatch thread the **identical pointer** — no proxy, no apartment
transition — so `OnChange` runs *on the dispatch thread*, concurrently with
main. `MgrPersistTest` §4 prints both thread ids and checks they differ.

Everything a handler touches in this tree is `Interlocked` or under a lock for
that reason. The pump stays anyway: depending on the CCW staying agile is how
you get a client that hangs on a customer's machine and not on yours.

The paging sink (§5 of the same file) is the same property seen from the other
side — agility is what lets the core call it synchronously, on the calling
thread, with no marshalling at all.

### 3. `ClassInterface(None)` is load-bearing, and costs `IDispatch`

With the default `AutoDispatch` the CCW exposes a generated class interface as
its `IDispatch`, and a dispinterface's DISPIDs resolve against **that** — every
event silently misrouted. With `None` the CCW's dispatch identity is the
declared interface and the ids line up.

The measured cost: `QueryInterface(IID_IDispatch)` on the CCW returns
`E_NOINTERFACE`. A managed sink is connectable **only** because
`CMsgStore::Advise` tries the DIID after `IID_IDispatch` fails
(`ComStore.cpp:1253`). A connection point that asked for `IID_IDispatch` alone
would refuse every C# client — and the same is true one tier up, in `TargetCom`.

### 4. Late binding is a different object again

`MgrCApiTest` uses no `[ComImport]` declaration at all — a ProgID and
`Type.InvokeMember`, which is what `New-Object -ComObject` compiles to. Three
things differ from the vtable face:

* **A binder failure and a target failure come out at different depths.** A
  misspelled member is a bare `COMException` carrying `DISP_E_UNKNOWNNAME`
  (0x80020006) — *not* a `MissingMemberException`, and *not* wrapped. A failure
  the server raised after the name resolved is a `COMException` inside a
  `TargetInvocationException`. A client that unwraps unconditionally is wrong
  about the first; one that never unwraps is wrong about the second.
* **`_NewEnum` does not come back as `IEnumVARIANT`.** The CLR substitutes
  `CustomMarshalers.EnumeratorViewOfEnumVariant`, a managed
  `System.Collections.IEnumerator` adapter. A client that only looks for the raw
  shape enumerates nothing, in silence. `Msgc.Api.Each` handles both.
* **A success code other than `S_OK` still does not survive.** `Cursor.Next`
  answers `S_FALSE` by vtable; ask `EndOfCursor` instead.

---

## Two shapes in the interop that are not transcription errors

Both are in [`MsgcoreComInterop.cs`](common/MsgcoreComInterop.cs), and both were
read off MIDL's own output rather than off the `.idl` by eye.

* **`IMsgVectCom.Item` cannot be a C# indexer.** Its getter is vtable slot 1 and
  its setter is slot 9 — the setter was appended with `Count` long after the
  getter — and a C# indexer emits its two accessors adjacently, which would
  shift eight members by one. Both container interfaces declare `Item` as a pair
  of ordinary methods so they read alike.
* **`P2PBridge.cs` declares eleven members it never calls.** A subset of an
  interface is only safe as a **prefix**, and `WsaQueryTest` needs `MsgTag`
  (slot 23) and `SendEx` (slot 25). Drop one of the members between and `SendEx`
  lands on `BroadcastEx`, whose first two `BSTR`s happen to line up — so it
  would not crash, it would broadcast every reply to every peer.

## One asymmetry in the server this tier found

`_IMsgStoreEvents::OnChange` declares its position as `hyper`, so it crosses as
`VT_I8` and arrives as a CLR `long`. The paging sink's position is a `VARIANT`
the server fills, and it fills it as **`VT_UI8`** — a CLR `ulong`. Same 64-bit
P2Pos, two VARIANT types. A C++ handler never notices, because it reads whichever
field it asks for; a C# one that writes `(long)p2pos` gets an
`InvalidCastException`. `Convert.ToInt64` is right for both.

---

## Layout

```
common\MsgcoreComInterop.cs   the type library, by hand -- all nine interfaces
common\ComHarness.cs          scoring, Api.Call, the two sinks, Each()
common\P2PBridge.cs           the TargetCom slice the two networked harnesses need
build.ps1                     Roslyn, then stage the five runtime DLLs
run_all.ps1                   register per-user, run, unregister, tally
<Name>\<Name>.cs              one harness each -> out\x64\<Config>\<Name>Net.exe
```

| harness | subject |
| --- | --- |
| `DataFieldTest` | the data model: typed cells, nodes, descendants, attributes, the error channel |
| `ListVectTest` | lists, vectors, nesting, the cursor, `For Each` |
| `MgrPersistTest` | a store as a document; triggers; **paging**; the agility measurement |
| `MgrCApiTest` | the same store **late-bound**, with no `[ComImport]` at all |
| `WsaStoreTest` | a whole store shipped between two hubs over loopback TCP |
| `WsaQueryTest` | the store stays put and is queried across the mesh, correlated by tag |
| `RecursTimeTest` | subtree walking, the real `IMsgRecursCom`, the stack, timestamps |
| `BstrWidthTest` | the heap, the three addressing widths, bounding without a sink |

`WsaStoreTest` and `WsaQueryTest` are the only two that need **both** servers —
`MsgcoreCom` for what is *in* a message, `TargetCom` for moving it. Neither
server knows about the other; the store becomes a byte array and the byte array
becomes a payload, which is the same relationship `Msgcore` and `Targetcore`
have one tier down.

## Prerequisites

Both COM servers must be **built** (this tree registers them itself, per-user,
and unregisters afterwards):

```powershell
..\ComExamples\run_all.ps1      # builds MsgcoreCom and TargetCom
```

`build.ps1` stages `MsgcoreCom.dll`, `TargetCom.dll`, `TargetFacade.dll`,
`Msgcore.dll` and `Targetcore.dll` into one directory and `run_all.ps1`
registers the staged copies — so there is **one** `Msgcore.dll` on the search
path. Registering the servers where they were built would have each resolve its
own copy through `LOAD_WITH_ALTERED_SEARCH_PATH`: same bytes, two modules, two
C-runtime states, two heaps.
