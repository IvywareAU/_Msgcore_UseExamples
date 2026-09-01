# WsaQueryTest — a Msgcore store as a service

The last harness in the tree. [`WsaStoreTest`](../WsaStoreTest) shipped a whole
store as one opaque blob; this is the other shape, and the more usual one: the
store **stays on the server**, and only small typed answers cross the wire.

```
client hub thread                                  server hub thread
─────────────────                                  ─────────────────
On_ConLoginAck                                     (owns a P2PmsgMgr catalogue)
  5 x StoreQuery ──[loopback TCP 127.0.0.1:7812]──► On_StoreQuery
                                                      RootPath2Object(path)
                                                      render type + value + P2Pos
  On_StoreReply ◄────────────────────────────────── StoreReply
    check type, value, pos
    5th reply => DONE
```

| | |
| --- | --- |
| **Msgcore** | the catalogue, `RootPath2Object` path lookup, the type tag that makes an answer self-describing, and `P2Pos` as a stable row handle |
| **TargetCore** | named messages routed by `BEGIN_P2PeerMsg_MAP` — each end only ever sees the messages addressed to it |

## The protocol

Deliberately trivial, because the interesting part is what sits behind it:

```
StoreQuery   payload = the query path, a NUL-terminated wide string
StoreReply   payload = "path|TYPE|value|p2pos"
```

A real protocol would carry a serialised Msgcore field rather than a delimited
string — [`WsaStoreTest`](../WsaStoreTest) shows how a Msgcore image becomes a
payload. This one stays readable so the routing is what you notice.

Note what the reply carries: the **type tag** travels with the value, so the
caller never has to guess how to read it, and the **`P2Pos`** gives it a stable
handle to ask again later without re-walking the path.

## One class, two roles

Routing is by **destination address**, so the server only ever sees `StoreQuery`
and the client only ever sees `StoreReply`. The unused handler on each end simply
never fires:

```cpp
BEGIN_P2PeerMsg_MAP(QueryHub, P2PeerHub)
    ON_P2PeerMsg(kMsgQuery, On_StoreQuery)   // server side
    ON_P2PeerMsg(kMsgReply, On_StoreReply)   // client side
END_P2PeerMsg_MAP()
```

Framework messages (BCast, Error, …) fall through to the base `P2PeerHub` map
beneath these entries. The same literal appears in `ON_P2PeerMsg` and in the
`P2PeerMsg32` constructor — names are matched as strings.

## A miss is an exception, not an empty result

`RootPath2Object` does **not** return a null object for an unknown path. It
throws a `P2Pevent` carrying *"Path to object does not exist"*. Any lookup
service over a Msgcore store therefore has to convert that throw into an answer:

```cpp
try   { P3PmsgField oField(m_pStore->RootPath2Object(strPath.c_str())); ... }
catch (P2Pevent* pEVT) { strReply = strPath + L"|MISS||0"; if (pEVT) pEVT->Cancel(false); }
```

Doing so is mandatory for a second reason: an exception escaping a hub handler is
caught by the pump, which then **drops the connection** (`P2Pwin32.cpp:3279`). A
client asking for a name that does not exist would silently lose the link. The
`..Missing.Node` query in the harness exists to pin that behaviour.

## Threading

The store is touched only from `On_StoreQuery`, on the server's single pump
thread, so it needs no lock — one hub, one pump, one thread (see
[`ArchitectureFAQ.md`](../../../_TargetCore_UseExamples/ArchitectureFAQ.md) Q3–Q6). The
catalogue is built in the constructor, on the main thread, before `SpawnHub()`
exists to race with it. Give the store a second reader and neither of those holds.

## This project does not delay-load

Unlike its siblings, `WsaQueryTest` links `TargetCore.dll` normally.
`BEGIN_P2PeerMsg_MAP` imports the **data** symbol `P2PeerHub::P2PeerMsgMap`, and
the linker refuses `/DELAYLOAD` on a DLL an image imports data from:

```
LINK : fatal error LNK1194: cannot delay-load 'TargetCore.dll' due to import of
data symbol '...P2PeerHub::P2PeerMsgMap'; link without /DELAYLOAD:TargetCore.dll
```

`_TargetCore_UseExamples\DirectExamples\PipeMsgMapTest` drops it for exactly the same reason.

## Build and run

```powershell
& $msbuild ".\WsaQueryTest(2022).vcxproj" /p:Configuration=Debug /p:Platform=x64
..\out\x64\Debug\WsaQueryTest.exe
```

Needs `TargetCore.dll` as well as `Msgcore.dll` in `..\..\..\bin\<Config>64`.

Exit `0` success · `1` setup · `2` assert · `3` timeout or a wrong answer.
Currently **18 checks, 0 failed, 5/5 answered**, Debug and Release.

## License

Copyright 2026 Khrustal & Mann, MELBOURNE, VICTORIA, AUSTRALIA, 3000.

Licensed under the Apache License, Version 2.0. See [`LICENSE`](../../LICENSE) for the
full text.
