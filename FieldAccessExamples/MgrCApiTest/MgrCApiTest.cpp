// Copyright © 2026 Khrustal & Mann
//              MELBOURNE, VICTORIA, AUSTRALIA, 3000
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
//
// MgrCApiTest.cpp
//
// THE FieldAccessExamples PORT of DirectExamples\MgrCApiTest. Its subject is
// still the flat C API, and every C call and check below is unchanged. What
// is new is a C++ side, written with Msgcore/MsgFieldRef.hpp (which is C++
// only), MEETING the C side on the same store:
//
//   * IN MEMORY -- Msgcore_c.h:72-74 documents every handle as the C++ object
//     cast to void*, and a MsgMgrHandle as a P2PmsgMgr. CppStore() takes that
//     at its word, so Field() and a typed view anchor on the very manager the
//     C calls are mutating;
//   * ON DISK -- section 4 saves through C and loads through C++, and back.
//
// Each section gains checks that the two sides agree: the C API writes and
// Field() reads it back, or the C++ side writes (typed view or Field()) and
// the C API reads. Section 2 adds the one difference that matters: a type
// mismatch on the C++ side is a CATCHABLE P2Pevent*, because the field
// layer is inline C++ in this file, not an extern "C" entry point.
//
// RULE 2 (Msgcore_c.h:103) applies across the bridge: a C++ write is a
// mutation, so no live C handle is held across one -- each is re-obtained
// afterwards.
//
// The SAME store, through the FLAT C API (Msgcore_c.h). No C++ classes, no
// operator overloads, no exceptions -- opaque handles and plain functions.
// No networking.
//
// This is the surface a non-C++ caller binds to: the P2P FileSystem core, a
// FUSE/WinFsp daemon, a Python or Rust FFI layer. Everything here maps onto
// the C++ calls the sibling harnesses make; the differences are all about
// ownership and lifetime, because C has no destructors to lean on.
//
// Two rules carry the whole API:
//
//   1. EVERY handle you are given, you must destroy. Even the ones that look
//      like they are just telling you something -- msgcore_field_declare_*
//      returns a handle to the field it declared, and dropping it leaks.
//
//   2. LIVE vs DETACHED is the difference between working and silently doing
//      nothing:
//        msgcore_mgr_root / msgcore_field_child      -> LIVE alias of the
//                                                       heap node; writes land
//        msgcore_mgr_as_field / *_select_item        -> DETACHED deep copy;
//                                                       writes are discarded
//      Reading is safe through either. Mutating through a detached handle
//      raises no error, returns success, and loses the change -- so reach for
//      the live pair whenever you intend to write.
//
// AND ONE SHARP EDGE, because "flat C API" implies a promise it does not keep:
// THE WRAPPERS ARE NOT UNIFORMLY EXCEPTION-SAFE. Structural calls
// (mgr_create, field_child, save, the trigger calls, get_int_any) wrap their
// bodies in try/catch and report failure as NULL or 0. The typed scalar
// accessors do NOT -- msgcore_field_get_int is literally
//
//     if (!hField) return 0;
//     return toField(hField)->c_int();          // Msgcore_c.cpp:~250
//
// so a type mismatch throws a P2Pevent* straight out of the DLL, across the
// C boundary.
//
// You cannot reliably catch it, either. The header is extern "C", and under
// MSVC's /EHsc model an extern "C" function is assumed not to throw -- so the
// optimiser may delete the handler you wrapped the call in. That is not
// hypothetical: an earlier revision of section 2 caught this throw in Debug
// and did NOT catch it in Release, in this same file.
//
// The rule that follows is therefore stricter than "handle the error": ASK
// THE TYPE BEFORE YOU READ IT (msgcore_field_get_data_type), or use a guarded
// accessor (msgcore_field_get_int_any). Section 2 shows both.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 2 = ASSERT, 3 = at least one check failed.

#include "stdafx.h"
#include "MgrCApiTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "Msgcore_c.h"
// Only for the section-2 demonstration that the strict accessors throw
// through the C boundary. A pure-C client has no equivalent and must dispatch
// on the type tag instead.
#include "Msgexception.h"

// The C++ side of the bridge -- not something a pure-C client can include.
#include "P2Pmsg.h"
#include "MsgDesc.h"
#include "P2PmsgMgr.h"
#include "MsgFieldRef.hpp"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CWinApp theApp;

// -------------------------------------------------------------------------
static int g_nChecks = 0;
static int g_nFailed = 0;

static void Check(bool bOk, LPCWSTR lpszWhat, int nLine)
{
    ++g_nChecks;
    if (bOk) return;
    ++g_nFailed;
    wprintf(L"  FAIL (line %d): %s\n", nLine, lpszWhat);
    fflush(stdout);
}
#define CHECK(expr) Check((expr), L#expr, __LINE__)

static void Section(LPCWSTR lpszTitle)
{
    wprintf(L"\n--- %s\n", lpszTitle);
    fflush(stdout);
}

static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fprintf(stderr, "\n=== ASSERT TRIPPED ===\n%s\n", szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;
        ExitProcess(2);
    }
    return FALSE;
}

static void MakeTempPath(wchar_t* szOut, LPCWSTR lpszLeaf)
{
    wchar_t szDir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, szDir);
    swprintf_s(szOut, MAX_PATH, L"%s%s", szDir, lpszLeaf);
}

static MsgMgrHandle NewMgr()
{
    return msgcore_mgr_create_nn(MSGCORE_ADDR_64, 4096, 1u << 20);
}

// The bridge. Msgcore_c.h:72-74: "all are heap-allocated C++ objects cast to
// void*", MsgMgrHandle -> P2PmsgMgr. The manager OBJECT holds still for the
// handle's life (only the heap under it moves), so it is a valid field anchor.
static P2PmsgMgr& CppStore(MsgMgrHandle hMgr)
{
    return *static_cast<P2PmsgMgr*>(hMgr);
}

// A C++ statement that must throw P2Pevent*. Usable here, unlike around a C
// call: the throw starts in C++ the compiler can see may throw.
template <class Fn>
static bool Refused(Fn fn)
{
    try { fn(); return false; }
    catch (P2Pevent* pEVT)
    {
        if (pEVT)
        {
            wprintf(L"  refused, as it should be: %s\n", (LPCWSTR)pEVT->GetMessage());
            pEVT->Cancel(false);
        }
        return true;
    }
}


// =========================================================================
// 1. Live vs detached
// =========================================================================
//
// The single most important thing to get right. Both calls hand back a
// MsgFieldHandle; only one of them is connected to the manager's heap.
//
static void Demo_LiveVsDetached()
{
    Section(L"1. LIVE vs DETACHED handles");

    MsgMgrHandle hMgr = NewMgr();
    CHECK(hMgr != nullptr);

    // --- through a DETACHED handle: the write goes nowhere ---------------
    {
        MsgFieldHandle hDetached = msgcore_mgr_as_field(hMgr);
        CHECK(hDetached != nullptr);
        MsgFieldHandle hDecl = msgcore_field_declare_wstr(hDetached, L"Ghost", L"x", 1);
        msgcore_field_destroy(hDecl);            // rule 1: destroy what you get
        msgcore_field_destroy(hDetached);
    }
    {
        MsgFieldHandle hLive = msgcore_mgr_root(hMgr);
        CHECK(msgcore_field_exists(hLive, L"Ghost") == 0);   // lost, as designed
        msgcore_field_destroy(hLive);
    }

    // --- through a LIVE handle: the write persists -----------------------
    {
        MsgFieldHandle hLive = msgcore_mgr_root(hMgr);
        CHECK(hLive != nullptr);
        MsgFieldHandle hDecl = msgcore_field_declare_wstr(hLive, L"Real", L"kept", 1);
        msgcore_field_destroy(hDecl);
        msgcore_field_destroy(hLive);
    }
    {
        MsgFieldHandle hLive = msgcore_mgr_root(hMgr);
        CHECK(msgcore_field_exists(hLive, L"Real") == 1);

        // ...and reading it back needs a child handle, which is also live.
        MsgFieldHandle hChild = msgcore_field_child(hLive, L"Real");
        CHECK(hChild != nullptr);
        CHECK(wcscmp(msgcore_field_get_wstr(hChild), L"kept") == 0);
        msgcore_field_destroy(hChild);
        msgcore_field_destroy(hLive);
    }

    // --- the C++ side sees the same store -------------------------------
    // C wrote "Real" through a live handle; Field() on the manager reads it.
    // The detached write is just as absent to C++ as it was to C.
    P2PmsgMgr& oMgr = CppStore(hMgr);
    CHECK(Field(oMgr, L"Real").AsText() == L"kept");
    CHECK(!Field(oMgr, L"Ghost").Exists());

    // ...and the other way: C++ writes, and the C API finds it.
    Field(oMgr, L"FromCpp") = L"seen";
    {
        MsgFieldHandle hLive = msgcore_mgr_root(hMgr);      // re-obtained after the write
        CHECK(msgcore_field_exists(hLive, L"FromCpp") == 1);
        MsgFieldHandle hChild = msgcore_field_child(hLive, L"FromCpp");
        CHECK(hChild != nullptr && wcscmp(msgcore_field_get_wstr(hChild), L"seen") == 0);
        msgcore_field_destroy(hChild);
        msgcore_field_destroy(hLive);
    }

    wprintf(L"  detached write discarded, live write persisted, C++ and C agree\n");
    msgcore_mgr_destroy(hMgr);
}


// =========================================================================
// 2. Typed declares and reads
// =========================================================================
//
// One declare_* per storage type; the type tag travels with the value and
// msgcore_type_name() renders it as the stable ASCII string the filesystem
// layer writes into a node's ".type".
//
// Reading a cell at the WRONG type is the hazard, and there are exactly two
// safe ways to avoid it:
//   (a) ask msgcore_field_get_data_type first and dispatch on the tag;
//   (b) use msgcore_field_get_int_any, which is guarded and reports failure
//       as 0 for anything that is not an integer-family scalar.
// Catching is not a third option -- see the file header.
//
// The section's six cells as a typed view, for the C++ side of the bridge.
// Each MSG_FIELD type is the C++ twin of a declare_* call: int <-> _int,
// long long <-> _int64, and so on -- the Typed coding writes the same tags.
struct Sample : MsgView
{
    MSG_FIELD ( Count,   int );
    MSG_FIELD ( Bytes,   long long );
    MSG_FIELD ( Ratio,   double );
    MSG_FIELD ( Enabled, bool );
    MSG_FIELD ( Label,   std::wstring );
    MSG_FIELD ( Raw,     MsgBlob );
};

static void Demo_TypedValues()
{
    Section(L"2. Typed declares and reads");

    MsgMgrHandle hMgr = NewMgr();
    MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);

    msgcore_field_destroy(msgcore_field_declare_int   (hRoot, L"Count",   42,        1));
    msgcore_field_destroy(msgcore_field_declare_int64 (hRoot, L"Bytes",   1LL << 40, 1));
    msgcore_field_destroy(msgcore_field_declare_double(hRoot, L"Ratio",   0.625,     1));
    msgcore_field_destroy(msgcore_field_declare_bool  (hRoot, L"Enabled", 1,         1));
    msgcore_field_destroy(msgcore_field_declare_wstr  (hRoot, L"Label",   L"widget", 1));

    struct { int nId; double d; } oBlob = { 9, 1.25 };
    msgcore_field_destroy(msgcore_field_declare_blob(hRoot, L"Raw", &oBlob, (int)sizeof(oBlob), 1));

    // Read each back through a live child handle and confirm both value and tag.
    struct { LPCWSTR lpszName; const char* lpszType; } aExpect[] = {
        { L"Count",   "INT32"  },
        { L"Bytes",   "INT64"  },
        { L"Ratio",   "DOUBLE" },
        { L"Enabled", "BOOL"   },
        { L"Label",   "WSTR16" },
        { L"Raw",     "BLOB16" },
    };

    for (int i = 0; i < (int)(sizeof(aExpect) / sizeof(aExpect[0])); i++)
    {
        MsgFieldHandle h = msgcore_field_child(hRoot, aExpect[i].lpszName);
        CHECK(h != nullptr);
        if (!h) continue;

        unsigned char uType = msgcore_field_get_data_type(h);
        const char*   pszTy = msgcore_type_name(uType);
        CHECK(strcmp(pszTy, aExpect[i].lpszType) == 0);

        // The name->code direction round-trips too (the ".type" read path).
        CHECK(msgcore_type_from_name(pszTy) == uType);

        wprintf(L"  %-8s type=%-7hs\n", aExpect[i].lpszName, pszTy);
        msgcore_field_destroy(h);
    }

    MsgFieldHandle h;
    h = msgcore_field_child(hRoot, L"Count");
    CHECK(msgcore_field_get_int(h) == 42);
    msgcore_field_set_int(h, 43);                       // live -> persists
    msgcore_field_destroy(h);

    h = msgcore_field_child(hRoot, L"Count");
    CHECK(msgcore_field_get_int(h) == 43);
    msgcore_field_destroy(h);

    h = msgcore_field_child(hRoot, L"Bytes");
    CHECK(msgcore_field_get_int64(h) == (1LL << 40));

    // (a) DISPATCH ON THE TAG. Ask first; never guess.
    CHECK(msgcore_field_get_data_type(h) == MSGCORE_DATA_INT64);

    // (b) THE TOLERANT READ. Guarded, width-agnostic, and it tells you the
    //     signedness -- the only integer accessor safe on an unknown cell.
    {
        long long iAny = 0; int bUnsigned = 1;
        CHECK(msgcore_field_get_int_any(h, &iAny, &bUnsigned) == 1);
        CHECK(iAny == (1LL << 40));
        CHECK(bUnsigned == 0);
    }

    // AND WHAT (c) WOULD HAVE BEEN -- "just wrap it in try/catch" -- IS NOT
    // AVAILABLE, which is why (a) and (b) are not merely tidier.
    //
    // msgcore_field_get_int on this INT64 cell throws
    // "Incompatible c_int() type (int64)" out of the DLL. In a Debug build a
    // surrounding catch(P2Pevent*) does catch it. In a RELEASE build of this
    // very harness it did not: everything in Msgcore_c.h is declared
    // extern "C", and under MSVC's /EHsc model an extern "C" function is
    // ASSUMED NOT TO THROW, so the optimiser is entitled to drop the handler
    // it thinks can never run -- and does. The throw then sails past the
    // try block to whatever catch survives further out, or to
    // std::terminate.
    //
    // So: a type mismatch on a strict accessor is not a recoverable
    // condition through this API, at any optimisation level. Check the tag
    // or use the guarded reader. The call is deliberately not made here.
    msgcore_field_destroy(h);

    h = msgcore_field_child(hRoot, L"Ratio");
    CHECK(msgcore_field_get_double(h) == 0.625);
    msgcore_field_destroy(h);

    h = msgcore_field_child(hRoot, L"Enabled");
    CHECK(msgcore_field_get_bool(h) == 1);
    msgcore_field_destroy(h);

    h = msgcore_field_child(hRoot, L"Raw");
    {
        int nSize = 0;
        const void* pv = msgcore_field_get_blob(h, &nSize);
        CHECK(pv != nullptr);
        CHECK(nSize == (int)sizeof(oBlob));
        CHECK(pv && memcmp(pv, &oBlob, sizeof(oBlob)) == 0);
    }
    msgcore_field_destroy(h);

    msgcore_field_destroy(hRoot);

    // --- C wrote, the typed view reads ----------------------------------
    // Every cell the declare_* calls made reads back through Sample with its
    // own type -- including Count, which C rewrote in place to 43.
    MsgViewOf<Sample> msg(CppStore(hMgr));
    CHECK(msg->Count.Get()   == 43);
    CHECK(msg->Bytes.Get()   == (1LL << 40));
    CHECK(msg->Ratio.Get()   == 0.625);
    CHECK(msg->Enabled.Get() == true);
    CHECK(msg->Label.Get()   == L"widget");
    CHECK(msg->Raw.Get()     == MsgBlob(&oBlob, sizeof(oBlob)));

    // --- the view writes, C reads ---------------------------------------
    msg->Count = 44;
    msg->Label = L"gadget";
    msg->Ratio = 0.5;
    {
        MsgFieldHandle hR = msgcore_mgr_root(hMgr);        // re-obtained after the writes
        h = msgcore_field_child(hR, L"Count");
        CHECK(strcmp(msgcore_type_name(msgcore_field_get_data_type(h)), "INT32") == 0);
        CHECK(msgcore_field_get_int(h) == 44);
        msgcore_field_destroy(h);

        h = msgcore_field_child(hR, L"Label");
        CHECK(msgcore_field_get_data_type(h) == MSGCORE_DATA_WSTR16);
        CHECK(wcscmp(msgcore_field_get_wstr(h), L"gadget") == 0);
        msgcore_field_destroy(h);

        h = msgcore_field_child(hR, L"Ratio");
        CHECK(msgcore_field_get_double(h) == 0.5);
        msgcore_field_destroy(h);
        msgcore_field_destroy(hR);
    }

    // --- the Bytes coding, seen from C ----------------------------------
    // MsgFieldCoding::Bytes stores an int as a 4-byte BLOB16, so a C reader
    // that called msgcore_field_get_int on it would throw across the
    // boundary. Rule (a) again: the tag says BLOB16, so read it as a blob.
    Field(MsgFieldAnchor::Of(CppStore(hMgr), MsgFieldCoding::Bytes), L"Wire") = 7;
    {
        MsgFieldHandle hR = msgcore_mgr_root(hMgr);
        h = msgcore_field_child(hR, L"Wire");
        CHECK(strcmp(msgcore_type_name(msgcore_field_get_data_type(h)), "BLOB16") == 0);
        int nSize = 0;
        const void* pv = msgcore_field_get_blob(h, &nSize);
        int nWire = 0;
        if (pv && nSize == (int)sizeof nWire) memcpy(&nWire, pv, sizeof nWire);
        CHECK(nSize == 4 && nWire == 7);
        msgcore_field_destroy(h);
        msgcore_field_destroy(hR);
    }
    CHECK(Field(CppStore(hMgr), L"Wire").AsInt() == 7);     // the C++ reader takes either

    // --- (c), on the C++ side only --------------------------------------
    // The same mismatch the C side may not attempt -- an int read of the
    // INT64 cell -- is refused with a P2Pevent* that CAN be caught here: the
    // field layer is inline C++, so no extern "C" frame lets the optimiser
    // drop the handler. Checked in Release as well as Debug.
    CHECK(Refused([&] { (void)msg->Bytes.Ref().AsInt(); }));
    CHECK(msg->Bytes.Get() == (1LL << 40));                 // and nothing was touched

    msgcore_mgr_destroy(hMgr);
}


// =========================================================================
// 3. Navigating and enumerating
// =========================================================================
//
// msgcore_field_child descends one level at a time, each step producing a
// handle you own. msgcore_curs_* walks a collection whose shape you do not
// know -- the C mirror of P3PmsgCurs.
//
static void Demo_Navigate()
{
    Section(L"3. Navigation and enumeration");

    MsgMgrHandle hMgr = NewMgr();

    // Build /Config/Network/{Host,Port}
    {
        MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);
        msgcore_field_destroy(msgcore_field_declare_wstr(hRoot, L"Config", L"", 1));

        MsgFieldHandle hCfg = msgcore_field_child(hRoot, L"Config");
        msgcore_field_destroy(msgcore_field_declare_wstr(hCfg, L"Network", L"", 1));

        MsgFieldHandle hNet = msgcore_field_child(hCfg, L"Network");
        msgcore_field_destroy(msgcore_field_declare_wstr(hNet, L"Host", L"127.0.0.1", 1));
        msgcore_field_destroy(msgcore_field_declare_int (hNet, L"Port", 7801,        1));

        msgcore_field_destroy(hNet);
        msgcore_field_destroy(hCfg);
        msgcore_field_destroy(hRoot);
    }

    // Descend and read.
    {
        MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);
        MsgFieldHandle hCfg  = msgcore_field_child(hRoot, L"Config");
        MsgFieldHandle hNet  = msgcore_field_child(hCfg,  L"Network");
        MsgFieldHandle hHost = msgcore_field_child(hNet,  L"Host");
        MsgFieldHandle hPort = msgcore_field_child(hNet,  L"Port");

        CHECK(hHost != nullptr && hPort != nullptr);
        CHECK(wcscmp(msgcore_field_get_wstr(hHost), L"127.0.0.1") == 0);
        CHECK(msgcore_field_get_int(hPort) == 7801);

        // A missing child is NULL, not an error to catch.
        CHECK(msgcore_field_child(hNet, L"Nope") == nullptr);

        // Enumerate the level with a cursor.
        int nSeen = 0;
        MsgCursHandle hCurs = msgcore_curs_from_field(hNet);
        CHECK(hCurs != nullptr);
        for (int i = 0; msgcore_curs_goto_index(hCurs, i); i++)
        {
            const wchar_t* lpszName = msgcore_curs_get_name(hCurs);
            wprintf(L"  Config.Network[%d] = %s (item=%d)\n",
                    i, lpszName ? lpszName : L"<null>", msgcore_curs_is_item(hCurs));
            ++nSeen;
        }
        msgcore_curs_destroy(hCurs);
        CHECK(nSeen == 2);

        msgcore_field_destroy(hPort);
        msgcore_field_destroy(hHost);
        msgcore_field_destroy(hNet);
        msgcore_field_destroy(hCfg);
        msgcore_field_destroy(hRoot);
    }

    // --- the same path, one expression on the C++ side ------------------
    // Five handles and five destroys above; [L"child"] walks the path C
    // built, and a read that misses creates nothing for C to find later.
    P2PmsgMgr& oMgr = CppStore(hMgr);
    CHECK(Field(oMgr, L"Config")[L"Network"][L"Host"].AsText() == L"127.0.0.1");
    CHECK(Field(oMgr, L"Config")[L"Network"][L"Port"].AsInt() == 7801);
    CHECK(!Field(oMgr, L"Config")[L"Proxy"][L"Host"].Exists());

    // A C++ write creates the path, and C descends it one handle at a time.
    Field(oMgr, L"Config")[L"Proxy"][L"Host"] = L"proxy.local";
    {
        MsgFieldHandle hRoot  = msgcore_mgr_root(hMgr);
        MsgFieldHandle hCfg   = msgcore_field_child(hRoot, L"Config");
        MsgFieldHandle hProxy = msgcore_field_child(hCfg,  L"Proxy");
        MsgFieldHandle hHost  = msgcore_field_child(hProxy, L"Host");
        CHECK(hProxy != nullptr && hHost != nullptr);
        CHECK(hHost && wcscmp(msgcore_field_get_wstr(hHost), L"proxy.local") == 0);
        msgcore_field_destroy(hHost);
        msgcore_field_destroy(hProxy);
        msgcore_field_destroy(hCfg);
        msgcore_field_destroy(hRoot);
    }

    msgcore_mgr_destroy(hMgr);
}


// =========================================================================
// 4. P2Pos, paths, and persistence
// =========================================================================
//
// The C API exposes the same positional handle the C++ side calls P2Pos. It
// is what a filesystem hands out as an inode: stable, integral, and
// resolvable back to a node or a path.
//
// msgcore_mgr_p2pos2path returns a THREAD-LOCAL buffer, valid only until the
// next call on the same thread. Copy it if you intend to keep it.
//
static void Demo_PosAndPersist()
{
    Section(L"4. P2Pos, paths, save/load");

    wchar_t szPath[MAX_PATH] = { 0 };
    MakeTempPath(szPath, L"mscs_example_capi.p2p");
    _wremove(szPath);

    unsigned long long pos = 0;

    // ---- writer --------------------------------------------------------
    {
        MsgMgrHandle hMgr = NewMgr();
        MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);
        msgcore_field_destroy(msgcore_field_declare_wstr(hRoot, L"Device", L"", 1));

        MsgFieldHandle hDev = msgcore_field_child(hRoot, L"Device");
        msgcore_field_destroy(msgcore_field_declare_wstr(hDev, L"Serial", L"SN-000123", 1));
        msgcore_field_destroy(msgcore_field_declare_int (hDev, L"Revision", 4, 1));

        MsgFieldHandle hSerial = msgcore_field_child(hDev, L"Serial");
        pos = msgcore_field_get_p2pos(hSerial);
        CHECK(pos != 0);

        const wchar_t* lpszPath = msgcore_mgr_p2pos2path(hMgr, pos);
        CHECK(lpszPath != nullptr && wcslen(lpszPath) > 0);
        wprintf(L"  pos %llu -> '%s'\n", pos, lpszPath ? lpszPath : L"<null>");

        // Resolve the position back to a field (a detached copy -- fine for
        // reading, which is all an inode lookup needs).
        MsgFieldHandle hByPos = msgcore_mgr_p2pos2field(hMgr, pos);
        CHECK(hByPos != nullptr);
        CHECK(wcscmp(msgcore_field_get_name(hByPos), L"Serial") == 0);
        CHECK(wcscmp(msgcore_field_get_wstr(hByPos), L"SN-000123") == 0);
        msgcore_field_destroy(hByPos);

        CHECK(msgcore_mgr_save(hMgr, szPath) == 1);
        CHECK(msgcore_mgr_is_dirty(hMgr) == 0);

        msgcore_field_destroy(hSerial);
        msgcore_field_destroy(hDev);
        msgcore_field_destroy(hRoot);
        msgcore_mgr_destroy(hMgr);       // closes the file
    }

    // ---- reader --------------------------------------------------------
    {
        MsgMgrHandle hMgr = msgcore_mgr_open_file(szPath);
        CHECK(hMgr != nullptr);
        CHECK(msgcore_mgr_is_valid(hMgr) == 1);

        MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);
        MsgFieldHandle hDev  = msgcore_field_child(hRoot, L"Device");
        MsgFieldHandle hSer  = msgcore_field_child(hDev,  L"Serial");
        CHECK(hSer != nullptr);
        CHECK(wcscmp(msgcore_field_get_wstr(hSer), L"SN-000123") == 0);

        // The position survived the round trip to disk and back.
        CHECK(msgcore_field_get_p2pos(hSer) == pos);
        wprintf(L"  reloaded, Serial still at pos %llu\n", pos);

        msgcore_field_destroy(hSer);
        msgcore_field_destroy(hDev);
        msgcore_field_destroy(hRoot);
        msgcore_mgr_destroy(hMgr);
    }

    // ---- C++ reader, then C++ writer, over the file C wrote ------------
    // The disk is the bridge that needs no cast at all: a P2PmsgMgr loads
    // what msgcore_mgr_save wrote, and Field() reads it by name.
    {
        P2PmsgMgr oCpp(szPath);
        CHECK(Field(oCpp, L"Device")[L"Serial"].AsText() == L"SN-000123");
        CHECK(Field(oCpp, L"Device")[L"Revision"].AsInt() == 4);
        // Plain: a position needs the live node, which a field ref never holds.
        CHECK((unsigned long long)oCpp.SelectItem(L"Device").SelectItem(L"Serial").GetP2Pos() == pos);

        Field(oCpp, L"Device")[L"Revision"] = 5;
        CHECK(oCpp.Save() != FALSE);                    // over its own file
    }   // closes the file

    // ---- and C reads what C++ saved -------------------------------------
    {
        MsgMgrHandle hMgr = msgcore_mgr_open_file(szPath);
        MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);
        MsgFieldHandle hDev  = msgcore_field_child(hRoot, L"Device");
        MsgFieldHandle hRev  = msgcore_field_child(hDev,  L"Revision");
        CHECK(hRev != nullptr && msgcore_field_get_data_type(hRev) == MSGCORE_DATA_INT32);
        CHECK(hRev && msgcore_field_get_int(hRev) == 5);
        wprintf(L"  C saved, C++ loaded and bumped Revision, C reloaded it: 5\n");
        msgcore_field_destroy(hRev);
        msgcore_field_destroy(hDev);
        msgcore_field_destroy(hRoot);
        msgcore_mgr_destroy(hMgr);
    }

    _wremove(szPath);
}


// =========================================================================
// 5. Structural edits -- rename, move, retype
// =========================================================================
//
// These three exist because a filesystem needs them and none of them is
// expressible as "declare over the top": a rename must keep the value, a
// move must keep the node, and a retype must replace the cell in place.
// All three act on a CHILD BY NAME of a live parent handle.
//
static void Demo_StructuralEdits()
{
    Section(L"5. Rename / move / retype");

    MsgMgrHandle hMgr = NewMgr();
    MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);

    msgcore_field_destroy(msgcore_field_declare_wstr(hRoot, L"Inbox",  L"", 1));
    msgcore_field_destroy(msgcore_field_declare_wstr(hRoot, L"Outbox", L"", 1));

    MsgFieldHandle hIn  = msgcore_field_child(hRoot, L"Inbox");
    MsgFieldHandle hOut = msgcore_field_child(hRoot, L"Outbox");
    msgcore_field_destroy(msgcore_field_declare_wstr(hIn, L"draft", L"hello", 1));

    // Rename in place: the value follows the name.
    CHECK(msgcore_field_rename_child(hIn, L"draft", L"letter") == 1);
    CHECK(msgcore_field_exists(hIn, L"draft") == 0);
    CHECK(msgcore_field_exists(hIn, L"letter") == 1);
    {
        MsgFieldHandle h = msgcore_field_child(hIn, L"letter");
        CHECK(wcscmp(msgcore_field_get_wstr(h), L"hello") == 0);
        msgcore_field_destroy(h);
    }

    // Move between parents: one node, two collections.
    CHECK(msgcore_field_move_child(hIn, hOut, L"letter") == 1);
    CHECK(msgcore_field_exists(hIn,  L"letter") == 0);
    CHECK(msgcore_field_exists(hOut, L"letter") == 1);
    {
        MsgFieldHandle h = msgcore_field_child(hOut, L"letter");
        CHECK(wcscmp(msgcore_field_get_wstr(h), L"hello") == 0);
        msgcore_field_destroy(h);
    }

    // Retype: replace the cell, changing the type tag with it.
    CHECK(msgcore_field_retype_child_int(hOut, L"letter", 99) == 1);
    {
        MsgFieldHandle h = msgcore_field_child(hOut, L"letter");
        CHECK(strcmp(msgcore_type_name(msgcore_field_get_data_type(h)), "INT32") == 0);
        CHECK(msgcore_field_get_int(h) == 99);
        msgcore_field_destroy(h);
    }
    wprintf(L"  draft -> letter, Inbox -> Outbox, WSTR16 -> INT32\n");

    // The C++ side sees all three edits. (Reads only: hIn and hOut are still
    // held, and rule 2 forbids a mutation under them.)
    P2PmsgMgr& oMgr = CppStore(hMgr);
    CHECK(!Field(oMgr, L"Inbox")[L"draft"].Exists());
    CHECK(!Field(oMgr, L"Inbox")[L"letter"].Exists());
    CHECK(Field(oMgr, L"Outbox")[L"letter"].DataType() == VBLockData_INT32);
    CHECK(Field(oMgr, L"Outbox")[L"letter"].AsInt() == 99);

    msgcore_field_destroy(hOut);
    msgcore_field_destroy(hIn);
    msgcore_field_destroy(hRoot);

    // Plain rename/move have no field-layer twin: a ref names a field, it
    // does not re-parent one. A RETYPE does -- a Field() write replaces the
    // tag with the value -- and C sees the cell change type back.
    Field(oMgr, L"Outbox")[L"letter"] = L"bye";
    {
        MsgFieldHandle hR = msgcore_mgr_root(hMgr);         // re-obtained after the write
        MsgFieldHandle hO = msgcore_field_child(hR, L"Outbox");
        MsgFieldHandle h  = msgcore_field_child(hO, L"letter");
        CHECK(h && strcmp(msgcore_type_name(msgcore_field_get_data_type(h)), "WSTR16") == 0);
        CHECK(h && wcscmp(msgcore_field_get_wstr(h), L"bye") == 0);
        msgcore_field_destroy(h);
        msgcore_field_destroy(hO);
        msgcore_field_destroy(hR);
    }

    msgcore_mgr_destroy(hMgr);
}


// =========================================================================
// 6. Change notification
// =========================================================================
//
// msgcore_trigger_fn and the C++ P2PmsgTriggerSink are the identical
// function-pointer type, so the C caller's callback becomes the heap sink
// directly -- no adapter, no per-call allocation.
//
struct TriggerCapture
{
    int                nCount;
    unsigned int       nLastType;
    unsigned long long nLastPos;
};

static void OnTrigger(void* pUser, unsigned int nType, unsigned long long pos)
{
    TriggerCapture* pCap = (TriggerCapture*)pUser;
    if (!pCap) return;
    pCap->nCount++;
    pCap->nLastType = nType;
    pCap->nLastPos  = pos;
}

static void Demo_Triggers()
{
    Section(L"6. Headless trigger sink");

    MsgMgrHandle hMgr = NewMgr();

    unsigned long long pos = 0;
    {
        MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);
        msgcore_field_destroy(msgcore_field_declare_wstr(hRoot, L"Watched", L"v", 1));
        MsgFieldHandle hLive = msgcore_field_child(hRoot, L"Watched");
        pos = msgcore_field_get_p2pos(hLive);
        msgcore_field_destroy(hLive);
        msgcore_field_destroy(hRoot);
    }
    CHECK(pos != 0);

    TriggerCapture oCap = { 0, 0, 0 };
    msgcore_mgr_set_trigger_sink(hMgr, &OnTrigger, &oCap);
    msgcore_mgr_create_trigger(hMgr, MSGCORE_TRIGGER_UPDATE | MSGCORE_TRIGGER_INSERT, pos);

    CHECK(msgcore_mgr_fire_trigger(hMgr, MSGCORE_TRIGGER_UPDATE, pos) == 1);
    CHECK(oCap.nCount == 1);
    CHECK(oCap.nLastType == MSGCORE_TRIGGER_UPDATE);
    CHECK(oCap.nLastPos == pos);

    CHECK(msgcore_mgr_fire_trigger(hMgr, MSGCORE_TRIGGER_INSERT, pos) == 1);
    CHECK(oCap.nCount == 2);

    // Disarm one leg; the other survives.
    msgcore_mgr_drop_triggers(hMgr, MSGCORE_TRIGGER_UPDATE, pos);
    CHECK(msgcore_mgr_fire_trigger(hMgr, MSGCORE_TRIGGER_UPDATE, pos) == 0);
    CHECK(oCap.nCount == 2);

    msgcore_mgr_set_trigger_sink(hMgr, nullptr, nullptr);
    wprintf(L"  sink saw %d notifications\n", oCap.nCount);

    // --- a C-armed trigger, fired by a C++ erase ------------------------
    // Arm DELETE through C; Field().Erase() deletes through the same
    // P3PmsgItem::Delete a C++ caller would use, and the C sink hears it.
    CHECK(Field(CppStore(hMgr), L"Watched").AsText() == L"v");   // C wrote it
    TriggerCapture oDel = { 0, 0, 0 };
    msgcore_mgr_set_trigger_sink(hMgr, &OnTrigger, &oDel);
    msgcore_mgr_create_trigger(hMgr, MSGCORE_TRIGGER_DELETE, pos);
    CHECK(Field(CppStore(hMgr), L"Watched").Erase());
    CHECK(oDel.nCount == 1);
    CHECK(oDel.nLastType == MSGCORE_TRIGGER_DELETE);
    CHECK(oDel.nLastPos == pos);
    {
        MsgFieldHandle hRoot = msgcore_mgr_root(hMgr);
        CHECK(msgcore_field_exists(hRoot, L"Watched") == 0);
        msgcore_field_destroy(hRoot);
    }
    msgcore_mgr_set_trigger_sink(hMgr, nullptr, nullptr);
    wprintf(L"  C++ erase fired the C-armed DELETE sink\n");

    msgcore_mgr_destroy(hMgr);
}


// =========================================================================
// main
// =========================================================================
int main(int /*argc*/, char* /*argv*/[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== MgrCApiTest - Msgcore through the flat C API ===\n");
    fflush(stdout);

    // A diagnostic net, not a safety mechanism: the file header explains why
    // a catch around an extern "C" call cannot be relied on. It is here so a
    // throw that DOES reach this far prints something useful instead of
    // aborting silently.
    try
    {
        Demo_LiveVsDetached();
        Demo_TypedValues();
        Demo_Navigate();
        Demo_PosAndPersist();
        Demo_StructuralEdits();
        Demo_Triggers();
    }
    catch (P2Pevent* pEVT)
    {
        ++g_nFailed;
        const CString strMessage = pEVT ? pEVT->GetMessage() : CString(L"<null>");
        wprintf(L"\nUNEXPECTED P2Pevent through the C boundary: %s\n", (LPCWSTR)strMessage);
        if (pEVT) pEVT->Cancel(false);
    }
    catch (...)
    {
        ++g_nFailed;
        wprintf(L"\nUNEXPECTED non-P2Pevent exception\n");
    }

    int nExit = (g_nFailed == 0) ? 0 : 3;
    wprintf(L"\n%d checks, %d failed. Done (exit=%d).\n", g_nChecks, g_nFailed, nExit);
    fflush(stdout);
    return nExit;
}
