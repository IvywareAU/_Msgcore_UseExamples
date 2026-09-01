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
// BstrWidthTest.cpp
//
// The STORAGE layer the other harnesses stand on without naming it:
// P3PmsgBSTR, the heap-plus-root-item that P2PmsgMgr and P2PeerMsg are both
// built from, and the ADDRESSING WIDTH that decides how big a heap's internal
// pointers are. No networking.
//
//   P3PmsgBSTR     a heap, a named root item, and a fixed set of well-known
//                  sections (Net, Sys, Evt, Wrp, Msg) addressed by flag
//                  rather than by name. TargetCore's P2PeerMsg derives from
//                  it directly -- an envelope IS one of these.
//
//   Addr08/16/32/64  the width of every offset inside the heap image. A
//                  narrower heap is smaller on the wire and caps out sooner.
//                  P3PmsgBSTR16/32/64 and P2PmsgMgr16/32/64 are the typedefs
//                  that pin one.
//
//   SafeRegistrationPush   RAII save/restore of a manager's paging callbacks.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 2 = ASSERT, 3 = at least one check failed.

#include "stdafx.h"
#include "BstrWidthTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pmsg.h"
#include "MsgDesc.h"
#include "P2PmsgBSTR.h"
#include "P2PmsgMgr.h"
#include "Msgexception.h"

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


// =========================================================================
// 1. P3PmsgBSTR -- a heap with a root item and named sections
// =========================================================================
//
// The (uVBLockAddr, nSizeof) constructor makes the heap; Init() puts the root
// item in it. Everything after that is ordinary Msgcore: the root is a
// P3PmsgItem, so it has a name, a data cell and descendants.
//
static void Demo_Bstr()
{
    Section(L"1. P3PmsgBSTR -- the heap under everything else");

    P3PmsgBSTR oBSTR(VBLock_Addr32, 4096);
    CHECK(oBSTR.GetP2Pmsgnn() == VBLock_Addr32);
    CHECK(oBSTR.Sizeof() > 0);

    oBSTR.Init(L"Envelope", P3PmsgData(L"payload"));

    // r_name() and r_data() are the root item seen as its two halves. On a
    // P3PmsgName the accessor is c_name(); c_wstr() is P3PmsgData's, for the
    // VALUE. A P3PmsgField has both, which is why the distinction only shows
    // up when you hold one half on its own.
    CHECK(wcscmp(oBSTR.r_name().c_name(), L"Envelope") == 0);
    CHECK(wcscmp(oBSTR.r_data().c_wstr(), L"payload") == 0);

    // r_item(VBLockBSTR_ROOT) is that same root item. On a P3PmsgField,
    // c_name() is the NAME half and c_wstr() the VALUE half.
    P3PmsgItem& oRoot = oBSTR.r_item(VBLockBSTR_ROOT);
    CHECK(wcscmp(oRoot.c_name(), L"Envelope") == 0);
    CHECK(wcscmp(oRoot.c_wstr(), L"payload") == 0);

    // The sections are the part that makes this more than a heap. They are
    // ordinary child items with reserved names, reached by FLAG rather than
    // by string -- which is how P2PeerMsg keeps its routing headers (Net)
    // apart from its application payload (Msg).
    CHECK(!oBSTR.Exists(VBLockBSTR_MSG));
    oBSTR.r_item(VBLockBSTR_MSG, /*bCreate*/ true);
    CHECK(oBSTR.Exists(VBLockBSTR_MSG));

    CHECK(!oBSTR.Exists(VBLockBSTR_SYS));
    oBSTR.r_item(VBLockBSTR_SYS, true);
    CHECK(oBSTR.Exists(VBLockBSTR_SYS));

    // A section is a normal item, so it takes descendants like any other.
    P3PmsgItem& oMsg = oBSTR.r_item(VBLockBSTR_MSG);
    oMsg.r_Desc(P3PmsgField::AttrCMD_Create);
    oMsg.DeclareItem(L"Verb", P3PmsgData(L"PUT"));
    oMsg.DeclareItem(L"Seq",  P3PmsgData((int)17));
    CHECK((int)oBSTR.r_item(VBLockBSTR_MSG).r_Desc().GetCount() == 2);
    CHECK(oBSTR.r_item(VBLockBSTR_MSG).SelectItem(L"Seq").c_int() == 17);

    CHECK(oBSTR.IsDirty());
    wprintf(L"  Addr32 envelope: heap %u bytes, 2 sections\n", (unsigned)oBSTR.Sizeof());
}


// =========================================================================
// 2. The addressing width
// =========================================================================
//
// Every offset stored inside a heap image is uAddrNN bits wide. The width is
// chosen at construction and is a property of the heap, not of the tree --
// the same tree costs different numbers of bytes at different widths, and a
// narrow heap simply cannot address a large one.
//
// P3PmsgBSTRnn<> exists so you can say the width once, in the type.
//
static void Demo_BstrWidths()
{
    Section(L"2. P3PmsgBSTR16 / 32 / 64 -- pinning the address width");

    P3PmsgBSTR16 oB16;
    P3PmsgBSTR32 oB32;
    P3PmsgBSTR64 oB64;

    CHECK(oB16.GetP2Pmsgnn() == VBLock_Addr16);
    CHECK(oB32.GetP2Pmsgnn() == VBLock_Addr32);
    CHECK(oB64.GetP2Pmsgnn() == VBLock_Addr64);

    // The width is not just a label: every control key in the heap's root
    // header is that many bits wide, so an empty heap already costs more at
    // 64 bits than at 16. This is the whole reason the knob exists.
    CHECK(oB16.Sizeof() < oB32.Sizeof());
    CHECK(oB32.Sizeof() < oB64.Sizeof());

    // The one-argument form asks for a bigger heap. Sizeof() still reports
    // what is COMMITTED, not what was requested -- the same distinction
    // P2PmsgMgr::Sizeof() makes -- so a bigger request is not a bigger
    // number until the tree actually needs the room.
    P3PmsgBSTR32 oSized(16384);
    CHECK(oSized.GetP2Pmsgnn() == VBLock_Addr32);
    CHECK(oSized.Sizeof() >= oB32.Sizeof());

    // The static default is what a plain P3PmsgBSTR uses when it has no heap
    // of its own to ask. Set it back afterwards -- it is process-wide state.
    const P2Pmsgnn_t uWas = P3PmsgBSTR::GetDefaultP2Pmsgnn();
    P3PmsgBSTR::SetDefaultP2Pmsgnn(VBLock_Addr16);
    CHECK(P3PmsgBSTR::GetDefaultP2Pmsgnn() == VBLock_Addr16);
    P3PmsgBSTR::SetDefaultP2Pmsgnn(uWas);
    CHECK(P3PmsgBSTR::GetDefaultP2Pmsgnn() == uWas);

    wprintf(L"  heaps: 16=%u  32=%u  64=%u bytes\n",
            (unsigned)oB16.Sizeof(), (unsigned)oB32.Sizeof(), (unsigned)oB64.Sizeof());
}


// =========================================================================
// 3. The same store at three widths
// =========================================================================
//
// P2PmsgMgr16/32/64 are the manager-level equivalents. The tree is identical;
// only the heap under it differs. This is the knob you turn when a store is
// going over a wire or into a small device, and the reason P2PmsgMgr's main
// constructor asks for uAddrNN first.
//
static void BuildSmallTree(P2PmsgMgr& oMgr)
{
    oMgr.r_Desc(P3PmsgField::AttrCMD_Create);
    oMgr.DeclareItem(L"Alpha", P3PmsgData((int)1));
    oMgr.DeclareItem(L"Beta",  P3PmsgData(L"two"));
    P3PmsgField& oGamma = oMgr.DeclareItem(L"Gamma", P3PmsgData(L""));
    oGamma.DeclareItem(L"Delta", P3PmsgData((double)4.5));
}

static void Demo_MgrWidths()
{
    Section(L"3. P2PmsgMgr16 / 32 / 64 -- the same tree, three heaps");

    P2PmsgMgr16 oMgr16;
    P2PmsgMgr32 oMgr32;
    P2PmsgMgr64 oMgr64;

    CHECK(oMgr16.IsValid());
    CHECK(oMgr32.IsValid());
    CHECK(oMgr64.IsValid());

    BuildSmallTree(oMgr16);
    BuildSmallTree(oMgr32);
    BuildSmallTree(oMgr64);

    // Identical content, whatever the width.
    CHECK((int)oMgr16.r_Desc().GetCount() == 3);
    CHECK((int)oMgr32.r_Desc().GetCount() == 3);
    CHECK((int)oMgr64.r_Desc().GetCount() == 3);
    CHECK(oMgr16.SelectItem(L"Alpha").c_int() == 1);
    CHECK(oMgr64.SelectItem(L"Alpha").c_int() == 1);
    CHECK(oMgr32.SelectItem(L"Gamma").SelectItem(L"Delta").c_double() == 4.5);

    // The explicit two-argument form sizes the heap.
    P2PmsgMgr32 oBig(32768, 1u << 20);
    CHECK(oBig.IsValid());
    BuildSmallTree(oBig);
    CHECK(oBig.Sizeof() > oMgr32.Sizeof());

    // All three report the same number here, because all three start from
    // the same 2024-byte request and none of them has outgrown it yet --
    // Sizeof() is committed bytes. The width shows up in the heap HEADER
    // (section 2) and in how far each can address, not in a small tree.
    wprintf(L"  same tree, committed: 16=%u  32=%u  64=%u bytes\n",
            (unsigned)oMgr16.Sizeof(), (unsigned)oMgr32.Sizeof(), (unsigned)oMgr64.Sizeof());
}


// =========================================================================
// 4. P3PmsgField16
// =========================================================================
//
// A P3PmsgField that inherits every constructor of its base and adds only a
// default one. It exists so a 16-bit-addressed context has a field type to
// name; behaviourally it IS a P3PmsgField.
//
static void Demo_Field16()
{
    Section(L"4. P3PmsgField16 -- a field, spelt for a 16-bit context");

    P3PmsgField16 oField;                      // default-constructed, unnamed

    // A field inherits P3PmsgName AND P3PmsgData, and BOTH declare c_size().
    // An unqualified call is ambiguous (C2385) -- say which half you mean.
    CHECK(oField.P3PmsgName::c_size() == 0);

    // The inherited constructors are all available. c_name() is the name;
    // c_wstr() would try to read the VALUE as a string and throw, because
    // this cell holds an int.
    P3PmsgField16 oNamed(L"Tag", P3PmsgData((int)9));
    CHECK(wcscmp(oNamed.c_name(), L"Tag") == 0);
    CHECK(oNamed.c_int() == 9);

    // And it slots into an ordinary tree, because it is an ordinary field.
    P2PmsgMgr oMgr(VBLock_Addr64, 4096, 1u << 20);
    oMgr.r_Desc(P3PmsgField::AttrCMD_Create);
    oMgr.r_Desc() += oNamed;
    CHECK(oMgr.r_Desc().Exists(L"Tag"));
    CHECK(oMgr.SelectItem(L"Tag").c_int() == 9);
}


// =========================================================================
// 5. Paging callbacks and SafeRegistrationPush
// =========================================================================
//
// A manager can hand the decision "this subtree is not in memory yet" back to
// the application. PageRegistration installs a page-in/page-out pair plus an
// opaque key; PageDatasetIn/Out invoke them.
//
// SafeRegistrationPush is the RAII bracket around SWAPPING that registration:
// the constructor saves the installed callbacks, the destructor puts them
// back. It does NOT suspend paging in between -- the live callbacks stay
// installed until something replaces them -- so it is a save/restore, not a
// disable.
//
static int g_nPageIn  = 0;
static int g_nPageOut = 0;
static int g_nAltIn   = 0;

static BOOL CALLBACK OnPageIn(PINT_PTR nKey, P2Pos posItem)
{
    if (nKey) ++g_nPageIn;
    (void)posItem;
    return TRUE;
}
static BOOL CALLBACK OnPageOut(PINT_PTR nKey, P2Pos posItem, BOOL bFlush)
{
    if (nKey) ++g_nPageOut;
    (void)posItem; (void)bFlush;
    return TRUE;
}
static BOOL CALLBACK OnAltPageIn(PINT_PTR nKey, P2Pos posItem)
{
    if (nKey) ++g_nAltIn;
    (void)posItem;
    return TRUE;
}

static void Demo_Paging()
{
    Section(L"5. Paging callbacks and SafeRegistrationPush");

    P2PmsgMgr oMgr(VBLock_Addr64, 4096, 1u << 20);
    oMgr.r_Desc(P3PmsgField::AttrCMD_Create);
    P3PmsgField& oItem = oMgr.DeclareItem(L"Dataset", P3PmsgData(L""));
    const P2Pos pos = oItem.GetP2Pos();

    // With nothing registered the calls are inert -- no callback, no crash.
    CHECK(!oMgr.PageDatasetIn(pos));
    CHECK(g_nPageIn == 0);

    // The key is any value the application wants back; it is what tells the
    // callback which object it is working for. It must be non-zero: the
    // manager uses it as the "is anything registered" test.
    INT_PTR nKey = (INT_PTR)&oMgr;
    oMgr.PageRegistration((PINT_PTR)nKey, &OnPageIn, &OnPageOut);

    CHECK(oMgr.PageDatasetIn(pos));
    CHECK(g_nPageIn == 1);
    CHECK(oMgr.PageDatasetOut(pos, FALSE));
    CHECK(g_nPageOut == 1);

    // Swap the registration for the duration of a scope, then have it put
    // back automatically.
    {
        SafeRegistrationPush oPushed(&oMgr);
        oMgr.PageRegistration((PINT_PTR)nKey, &OnAltPageIn, &OnPageOut);

        CHECK(oMgr.PageDatasetIn(pos));
        CHECK(g_nAltIn == 1);
        CHECK(g_nPageIn == 1);          // the original did not fire
    }

    // Out of scope: the original pair is back.
    CHECK(oMgr.PageDatasetIn(pos));
    CHECK(g_nPageIn == 2);
    CHECK(g_nAltIn == 1);

    wprintf(L"  page-in %d, page-out %d, alternate %d\n", g_nPageIn, g_nPageOut, g_nAltIn);
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

    wprintf(L"=== BstrWidthTest - the heap, its width, and paging ===\n");
    fflush(stdout);

    try
    {
        Demo_Bstr();
        Demo_BstrWidths();
        Demo_MgrWidths();
        Demo_Field16();
        Demo_Paging();
    }
    catch (P2Pevent* pEVT)
    {
        ++g_nFailed;
        const CString strMessage = pEVT ? pEVT->GetMessage() : CString(L"<null>");
        wprintf(L"\nUNEXPECTED P2Pevent: %s\n", (LPCWSTR)strMessage);
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
