# WsaStoreTest — a Msgcore store shipped between two hubs

The first harness in this tree that links **both** libraries. It answers the
question the four pure-Msgcore harnesses leave open: *how does a Msgcore tree
become a message?*

```
client hub thread                                    server hub thread
─────────────────                                    ─────────────────
On_ConLoginAck
  BuildStore()          P2PmsgMgr, 5 top-level nodes
  Save() + read back    3595 bytes of heap image
  PostP2PeerMsg ──────[loopback TCP 127.0.0.1:7811]──► On_P2PeerBCast
                                                         write bytes to a file
                                                         P2PmsgMgr::Load()
                                                         verify every field
                                                         SetEvent  =>  DONE
```

The division of labour is the point:

| | |
| --- | --- |
| **Msgcore** | owns the tree and renders it as a flat heap image |
| **Targetcore** | carries an opaque byte range from one hub to another |

Targetcore has no idea what is in the payload. Making those bytes a Msgcore store
is entirely the application's business, and this harness is that application.

## The mesh

Exactly [`_Targetcore_UseExamples\DirectExamples/WsaMeshTest`](../../../_Targetcore_UseExamples/DirectExamples/WsaMeshTest)'s:
two `P2PeerHub`s in one process, each on its own `SpawnHub()` pump thread,
connected by `P2PeerConWsa` over `127.0.0.1:7811`. The server's
`ServiceFactory` binds and listens; a `Sleep(750)` lets its pump reach
`Listen()`/`Accept()` before the client's `ClientFactory` dials.

**The side that dialled speaks first.** The listening hub's own login leg
completes at a different instant, so a message posted before the ack has nowhere
to go. The send is therefore in `On_ConLoginAck`, on the client only.

For anything else about the mesh, see
[`../../../_Targetcore_UseExamples/ArchitectureFAQ.md`](../../../_Targetcore_UseExamples/ArchitectureFAQ.md).

## Why the image goes through a file

A `P2PmsgMgr`'s heap is already one contiguous, relocation-safe image — that is
why `Save()`/`Load()` can be a plain block write, and it is what makes shipping a
store sensible at all.

But the in-memory accessors for that image — `P2PmsgHeap_pImage`,
`P2PmsgHeap_pIOmage`, `P2PmsgHeap_CreateIOMAGE` (`MsgVBHeap.h:118-157`) — carry
**no `Msgcore_EXT`**, so they are not exported from the DLL, and
`P2PmsgMgr::m_hMgr` is `protected`. A client cannot reach the bytes directly.

The exported route to the same bytes is `Save()` to a file and read the file
back, which is what `StoreToBytes` / `BytesToStore` do. Each side owns its **own**
scratch file, so nothing is shared behind the socket's back — the bytes really do
travel through the mesh. Exporting those five functions would remove the round
trip; the extra I/O is a limitation of the export surface, not of the format.

`Load()` validates the image before trusting it, so a truncated or corrupted
payload fails there rather than corrupting the heap.

## Size

One `P2PeerMsg` is capped at `MAX_P2Psize = 32768` bytes (`P2PeerMsg.h:36`). The
store here is built in a 2 KB heap so the image sits comfortably inside a single
frame — it comes out at about 3.6 KB. A real store would need chunking, and the
harness refuses to post rather than truncate.

## Handling exceptions on a hub thread

The rebuild runs inside `On_P2PeerBCast`, on the server's pump thread. An
exception escaping a hub handler is caught by the pump, which then **drops the
connection** (`P2Pwin32.cpp:3279`). So the rebuild is wrapped, and a failure is
recorded as a failed check rather than allowed to kill the link.

Checks run on the hub thread and are read by `main` after the done event, which
is the barrier that makes that safe; the counters are `Interlocked`.

## Build and run

```powershell
& $msbuild ".\WsaStoreTest(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\WsaStoreTest.exe
```

Needs `Targetcore.dll` as well as `Msgcore.dll` in `..\..\..\bin\<Config>64`.

Exit `0` success · `1` setup · `2` assert · `3` timeout or mismatch. Currently
**13 checks, 0 failed**, Debug and Release.

Next: [`WsaQueryTest`](../WsaQueryTest) — the store stays put and is queried
instead.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for the
full text.
