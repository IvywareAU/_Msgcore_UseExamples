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
// MgrPersistTest.cpp
//
// P2PmsgMgr -- the Msgcore STORE: a heap that owns a whole tree, can be
// written to a file and read back, addresses any node by a stable position,
// and fires change notifications. No networking.
//
// Everything in DataFieldTest and ListVectTest built loose objects, each one
// owning its own little heap. A P2PmsgMgr is the container that makes a tree
// a *document*:
//
//   * one heap for the entire tree, so the whole graph is contiguous;
//   * Save()/Load() to a .p2p file, addresses and all;
//   * P2Pos -- a stable positional handle to any node, survivable across
//     Save/Load, which is exactly what the TreeFs layer uses as an inode;
//   * triggers -- INSERT/UPDATE/DELETE notification, with a headless
//     function-pointer sink for hosts that have no window to post to.
//
// The manager IS a P3PmsgItem (P2PmsgMgr.h:163), so every tree-building call
// you already know -- DeclareItem, SelectItem, r_Desc() -- works on it
// directly. The manager is simply the root node that owns the heap.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 2 = ASSERT, 3 = at least one check failed.

#include "stdafx.h"
#include "MgrPersistTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pmsg.h"
#include "MsgVect.h"
#include "MsgDesc.h"
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

// Scratch path under the OS temp area.
static void MakeTempPath(wchar_t* szOut, LPCWSTR lpszLeaf)
{
    wchar_t szDir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, szDir);
    swprintf_s(szOut, MAX_PATH, L"%s%s", szDir, lpszLeaf);
}

// Build the same little tree in any manager, so the save and the reload can
// be compared against one description.
static void BuildInventory(P2PmsgMgr& oMgr)
{
    oMgr.r_Desc(P3PmsgField::AttrCMD_Create);

    oMgr.DeclareItem(L"Warehouse", P3PmsgData(L"Melbourne-01"));
    oMgr.DeclareItem(L"Capacity",  P3PmsgData((int)50000));
    oMgr.DeclareItem(L"Utilised",  P3PmsgData((double)0.735));

    P3PmsgField& oStock = oMgr.DeclareItem(L"Stock", P3PmsgData(L""));
    oStock.DeclareItem(L"SKU-1001", P3PmsgData((int)412));
    oStock.DeclareItem(L"SKU-1002", P3PmsgData((int)88));
    oStock.DeclareItem(L"SKU-1003", P3PmsgData((int)0));

    // A vect goes in whole; the heap stores its element addresses as image
    // offsets, so the array survives serialisation intact.
    P3PmsgVect oDaily(7, L"DailyOut", P3PmsgData((int)0));
    for (int i = 0; i < 7; i++)
        oDaily.r_data(i).c_int((i + 1) * 11);
    oMgr.r_Desc() += oDaily;
}


// =========================================================================
// 1. Build a store and look at it
// =========================================================================
//
// The (uAddrNN, nSizeInitial, nSizeMax) constructor picks the heap's address
// width and its growth bounds. Addr64 is the safe default for anything that
// might get large; Addr16/Addr32 buy compactness at a size ceiling. The heap
// grows on demand from nSizeInitial up to nSizeMax and no further.
//
static void Demo_BuildAndInspect()
{
    Section(L"1. P2PmsgMgr -- a heap that owns a tree");

    P2PmsgMgr oMgr(VBLock_Addr64, 4096, 1u << 20);
    CHECK(oMgr.IsValid());

    // Sizeof() is the heap's CURRENT ALLOCATION, not the bytes in use. It
    // starts at nSizeInitial (plus the header) and only moves when the tree
    // outgrows it -- so a small tree in a 4 KB heap reports 4 KB before and
    // after. Do not read it as "how big is my data".
    VBLsize nEmpty = oMgr.Sizeof();
    BuildInventory(oMgr);
    VBLsize nFull  = oMgr.Sizeof();

    CHECK(nFull >= nEmpty);
    CHECK(oMgr.IsDirty());                    // unsaved changes are tracked

    CHECK(oMgr.Exists(L"Warehouse"));
    CHECK(oMgr.Exists(L"Stock"));
    CHECK((int)oMgr.r_Desc().GetCount() == 5);
    CHECK(oMgr.SelectItem(L"Capacity").c_int() == 50000);
    CHECK((int)oMgr.SelectItem(L"Stock").r_Desc().GetCount() == 3);

    wprintf(L"  heap %llu -> %llu bytes for 5 top-level nodes (fits the initial 4 KB)\n",
            (unsigned long long)nEmpty, (unsigned long long)nFull);

    // Force the growth path: a deliberately tiny initial heap, filled past it.
    // The heap expands on demand towards nSizeMax and the tree is unaffected.
    P2PmsgMgr oSmall(VBLock_Addr64, 512, 1u << 20);
    VBLsize nStart = oSmall.Sizeof();
    oSmall.r_Desc(P3PmsgField::AttrCMD_Create);
    for (int i = 0; i < 300; i++)
    {
        wchar_t szName[32];
        swprintf_s(szName, 32, L"node%03d", i);
        oSmall.DeclareItem(szName, P3PmsgData((int)i));
    }
    CHECK(oSmall.Sizeof() > nStart);
    CHECK((int)oSmall.r_Desc().GetCount() == 300);
    CHECK(oSmall.SelectItem(L"node299").c_int() == 299);
    wprintf(L"  heap %llu -> %llu bytes after 300 nodes in a 512-byte start\n",
            (unsigned long long)nStart, (unsigned long long)oSmall.Sizeof());
}


// =========================================================================
// 2. Save and load
// =========================================================================
//
// Save() writes the heap image and then HOLDS THE FILE OPEN for the
// manager's lifetime (default share mode FILE_SHARE_READ, P2PmsgMgr.h:307).
// Other readers may open it; other writers may not. That is why the writer
// below lives in its own scope -- the reader must not race a live writer.
//
// bDefragment=true compacts the heap on the way out, which is what you want
// for a store that has seen a lot of deletes.
//
static void Demo_SaveLoad()
{
    Section(L"2. Save / Load -- the store as a document");

    wchar_t szPath[MAX_PATH] = { 0 };
    MakeTempPath(szPath, L"mscs_example_inventory.p2p");
    _wremove(szPath);

    VBLsize nSaved = 0;

    // ---- writer -------------------------------------------------------
    {
        P2PmsgMgr oMgr(VBLock_Addr64, 4096, 1u << 20);
        BuildInventory(oMgr);

        CHECK(oMgr.Save(szPath, /*bDefragment*/ true) != FALSE);
        nSaved = oMgr.Sizeof();

        // Saving clears the dirty flag and records the filename.
        CHECK(!oMgr.IsDirty());
        CHECK(oMgr.GetFilename() != nullptr);
        wprintf(L"  saved  : %s\n", oMgr.GetFilename());
    }   // writer closes the file here

    CHECK(P2PmsgMgr_IsValid(szPath));        // recognisable as a store on disk

    // ---- reader -------------------------------------------------------
    {
        P2PmsgMgr oLoaded(szPath);           // ctor-from-filename == Load()
        CHECK(oLoaded.IsValid());
        CHECK(oLoaded.Sizeof() == nSaved);

        // Every scalar came back with its type and value intact.
        CHECK(oLoaded.Exists(L"Warehouse"));
        CHECK(wcscmp(oLoaded.SelectItem(L"Warehouse").c_wstr(), L"Melbourne-01") == 0);
        CHECK(oLoaded.SelectItem(L"Capacity").c_int() == 50000);
        CHECK(oLoaded.SelectItem(L"Utilised").c_double() == 0.735);

        // ...and so did the nested subtree.
        CHECK(oLoaded.SelectItem(L"Stock").Exists(L"SKU-1002"));
        CHECK(oLoaded.SelectItem(L"Stock").SelectItem(L"SKU-1002").c_int() == 88);

        // ...and the vect, whose internal element addresses had to be
        // re-based against the loaded image.
        P3PmsgVect oDaily(oLoaded.r_Desc().SelectVect(L"DailyOut").r_Object());
        CHECK((int)oDaily.GetCount() == 7);
        CHECK(oDaily.r_data(0).c_int() == 11);
        CHECK(oDaily.r_data(6).c_int() == 77);

        wprintf(L"  loaded : %d nodes, DailyOut[0..6] = %d..%d\n",
                (int)oLoaded.r_Desc().GetCount(),
                oDaily.r_data(0).c_int(), oDaily.r_data(6).c_int());
    }

    // ---- round two: mutate the reloaded store and save it again -------
    {
        P2PmsgMgr oMgr(szPath);
        oMgr.SelectItem(L"Stock").DeclareItem(L"SKU-1004", P3PmsgData((int)7));
        oMgr.SelectItem(L"Capacity").c_int(60000);
        CHECK(oMgr.IsDirty());
        CHECK(oMgr.Save() != FALSE);         // no filename == save over itself
        CHECK(!oMgr.IsDirty());
    }
    {
        P2PmsgMgr oMgr(szPath);
        CHECK(oMgr.SelectItem(L"Capacity").c_int() == 60000);
        CHECK((int)oMgr.SelectItem(L"Stock").r_Desc().GetCount() == 4);
    }

    _wremove(szPath);
}


// =========================================================================
// 3. P2Pos -- a stable handle to a node
// =========================================================================
//
// A C++ reference into the tree is only good while the tree holds still. A
// P2Pos is an offset within the heap image, so it stays valid across
// Save/Load and can be stored, passed around, or handed to a trigger. This
// is the mechanism the P2P FileSystem uses to give a node an inode number.
//
// The one caution: GetP2Pos() means something only on a LIVE node -- one
// reached through the manager's own tree (SelectItem returns a reference to
// the real node). A detached deep copy has a position in its own private
// heap, which addresses nothing in the manager.
//
static void Demo_Addressing()
{
    Section(L"3. P2Pos -- addressing a node by position");

    P2PmsgMgr oMgr(VBLock_Addr64, 4096, 1u << 20);
    BuildInventory(oMgr);

    // Take the position of a leaf two levels down.
    P3PmsgField& oLive = oMgr.SelectItem(L"Stock").SelectItem(L"SKU-1002");
    P2Pos pos = oLive.GetP2Pos();
    CHECK(pos != 0);
    CHECK(oMgr.IsField(pos));

    // ...and go back the other way.
    P3PmsgField oByPos = oMgr.P2Pos2Field(pos);
    CHECK(oByPos == L"SKU-1002");
    CHECK(oByPos.c_int() == 88);

    // The path form is human-readable: dot-separated names, rooted at the
    // manager -- e.g. "..Stock.SKU-1002" for the node two levels down.
    const CString strPath = oMgr.P2Pos2Path(pos);
    CHECK(strPath.GetLength() > 0);
    wprintf(L"  pos %llu -> path '%s'\n",
            (unsigned long long)pos, (LPCWSTR)strPath);

    // GetPath() on the live node answers the same question from the node's
    // side, and must agree.
    const CString strFromNode = oLive.GetPath();
    CHECK(strFromNode == strPath);
    wprintf(L"  node GetPath()  '%s'\n", (LPCWSTR)strFromNode);

    // ...and the path resolves back to the same node. That closes the loop:
    // reference -> P2Pos -> path -> object, all naming one heap node.
    P3PmsgField oFromPath(oMgr.RootPath2Object((LPCWSTR)strPath));
    CHECK(oFromPath == L"SKU-1002");
    CHECK(oFromPath.c_int() == 88);
    wprintf(L"  path -> field   '%s' = %d\n",
            oFromPath.r_name().c_name(), oFromPath.c_int());
}


// =========================================================================
// 4. Triggers -- change notification without a window
// =========================================================================
//
// Msgcore's original trigger path posts a Windows message to an HWND. A
// daemon, a FUSE mount or a console harness has no window, so the manager
// also carries a plain function-pointer sink (P2PmsgTriggerSink, Msgcore.h:121)
// fired alongside the HWND path for every armed node.
//
// Arming is per NODE and per MASK: CreateTrigger(mask, hWnd, pos). With
// hWnd == NULL the PostMessage leg is a harmless no-op and the sink is the
// only delivery. FireTrigger() raises one manually; a real DELETE through the
// tree raises one by itself.
//
struct TriggerCapture
{
    int                 nCount;
    UINT                nLastType;
    unsigned long long  nLastPos;
};

static void CALLBACK OnTrigger(void* pUser, UINT nType, unsigned long long pos)
{
    TriggerCapture* pCap = (TriggerCapture*)pUser;
    if (!pCap) return;
    pCap->nCount++;
    pCap->nLastType = nType;
    pCap->nLastPos  = pos;
}

static void Demo_Triggers()
{
    Section(L"4. Triggers -- headless change notification");

    P2PmsgMgr oMgr(VBLock_Addr64, 4096, 1u << 20);
    BuildInventory(oMgr);

    P2Pos posWatched = oMgr.SelectItem(L"Stock").SelectItem(L"SKU-1001").GetP2Pos();
    CHECK(posWatched != 0);

    TriggerCapture oCap = { 0, 0, 0 };
    oMgr.SetTriggerSink(&OnTrigger, &oCap);
    oMgr.CreateTrigger(TRIGGER_UPDATE | TRIGGER_INSERT, (HWND)nullptr, posWatched);

    // Fire UPDATE by hand -- the host tells the manager "this node changed".
    CHECK(oMgr.FireTrigger(posWatched, TRIGGER_UPDATE) == 1);
    CHECK(oCap.nCount == 1);
    CHECK(oCap.nLastType == TRIGGER_UPDATE);
    CHECK(oCap.nLastPos == (unsigned long long)posWatched);

    // The INSERT arm is a separate registration on the same node.
    CHECK(oMgr.FireTrigger(posWatched, TRIGGER_INSERT) == 1);
    CHECK(oCap.nCount == 2);
    CHECK(oCap.nLastType == TRIGGER_INSERT);

    // An unarmed mask delivers nothing. (Do not use DELETE as a probe: a
    // DELETE pass is read as "the object is gone" and drops every
    // registration on the node.)
    CHECK(oMgr.FireTrigger(posWatched, TRIGGER_ACTIVE) == 0);
    CHECK(oCap.nCount == 2);

    // Disarm the UPDATE leg; INSERT survives it.
    oMgr.DropTriggers(TRIGGER_UPDATE, (HWND)nullptr, posWatched);
    CHECK(oMgr.FireTrigger(posWatched, TRIGGER_UPDATE) == 0);
    CHECK(oMgr.FireTrigger(posWatched, TRIGGER_INSERT) == 1);
    CHECK(oCap.nCount == 3);

    // Clearing the sink stops delivery even while registrations remain.
    oMgr.SetTriggerSink(nullptr, nullptr);
    CHECK(oMgr.FireTrigger(posWatched, TRIGGER_INSERT) == 1);
    CHECK(oCap.nCount == 3);

    wprintf(L"  sink saw %d notifications, last type=%u\n",
            oCap.nCount, oCap.nLastType);

    // A genuine DELETE of an armed node fires the sink on its own.
    TriggerCapture oDel = { 0, 0, 0 };
    P2Pos posDoomed = oMgr.SelectItem(L"Stock").SelectItem(L"SKU-1003").GetP2Pos();
    oMgr.SetTriggerSink(&OnTrigger, &oDel);
    oMgr.CreateTrigger(TRIGGER_DELETE, (HWND)nullptr, posDoomed);

    CHECK(oMgr.SelectItem(L"Stock").Delete(L"SKU-1003"));
    CHECK(oDel.nCount == 1);
    CHECK(oDel.nLastType == TRIGGER_DELETE);
    CHECK(oDel.nLastPos == (unsigned long long)posDoomed);
    wprintf(L"  delete of SKU-1003 auto-fired the DELETE sink\n");

    oMgr.SetTriggerSink(nullptr, nullptr);
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

    wprintf(L"=== MgrPersistTest - the Msgcore store: save, address, notify ===\n");
    fflush(stdout);

    try
    {
        Demo_BuildAndInspect();
        Demo_SaveLoad();
        Demo_Addressing();
        Demo_Triggers();
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
