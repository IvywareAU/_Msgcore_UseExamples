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
// RecursTimeTest.cpp
//
// The two parts of the Msgcore model the other six harnesses never reach:
// P2PmsgRecurs, the RECURSIVE tree walker, and P3PmsgTime, the TIME64 cell.
// No networking.
//
//   P2PmsgRecurs   P3PmsgCurs walks ONE level. P2PmsgRecurs walks a whole
//                  subtree from a single flat loop -- it owns a chain of
//                  cursors, one per level, and splices descent into
//                  operator++. What it does NOT do is descend on its own:
//                  the caller decides, by calling Push(), which is what
//                  makes it a walker rather than an iterator.
//
//   P3PmsgTime     a P3PmsgData specialisation carrying VBLockData_TIME64 --
//                  a 64-bit time_t, the same value CTime holds. It is the
//                  only typed subclass of P3PmsgData in the library.
//
// FIELD-ACCESS PORT. This is the FieldAccessExamples port of
// DirectExamples\RecursTimeTest. The subjects and every check are unchanged;
// what changed is how NAMED fields are written and read, which is now
// Msgcore/MsgFieldRef.hpp:
//
//   * the tree every section walks is BUILT through typed views
//     (store->Company = L"Ivyware", product->Build = 4210) and the dynamic
//     form (Field(oMgr, L"Notes")[L"Memo"] = L"hello"). Views on a NESTED
//     item are anchored by MsgFieldAnchor::Child, chained once per level.
//   * section 3 reads the children of a node the walker is standing on
//     through a view bound to oRec.r_item(), and checks it against what the
//     walker's own r_data() reported.
//   * section 5 writes and reads the record's text and an int64 timestamp by
//     name, before and after Save/Load.
//
// What stays plain: the walker itself (P2PmsgRecurs, Push/Pop/Break), the
// one-level cursor, the list and vect, the unnamed P3PmsgTime cells of
// section 4, and Save/Load. Section 5's TIME64 FIELD goes in by name as a
// MsgTime (msg->Created = MsgTime(t)), which stores the same TIME64 cell a
// P3PmsgTime does.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 2 = ASSERT, 3 = at least one check failed.

#include "stdafx.h"
#include "RecursTimeTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pmsg.h"
#include "MsgList.h"
#include "MsgVect.h"
#include "MsgDesc.h"
#include "MsgCurs.h"
#include "P2PmsgMgr.h"
#include "Msgexception.h"
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

// A statement that must throw P2Pevent* -- the library's only exception.
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

// The fixed schemas of the tree below, one view per level.
struct StoreView : MsgView          // the manager's top level
{
    MSG_FIELD ( Company, std::wstring );
    MSG_FIELD ( Notes,   std::wstring );
};
struct CompanyView : MsgView        // under Company
{
    MSG_FIELD ( Product, std::wstring );
    MSG_FIELD ( Founded, int );
};
struct ProductView : MsgView        // under Company/Product
{
    MSG_FIELD ( Version, std::wstring );
    MSG_FIELD ( Build,   int );
};

// A walk that cannot hang. Every loop over a P2PmsgRecurs in this file is
// bounded: a traversal bug in the library should fail the harness, not wedge
// it, because a wedged test tells you nothing and blocks the build.
static const int kMaxSteps = 500;

// -------------------------------------------------------------------------
// The tree every section walks.
//
//   Company                     "Ivyware"
//     Product                   "Chartboard"
//       Version                 "3.1"
//       Build                   4210
//     Founded                   1996
//   Notes                       "misc"
//     Memo                      "hello"
//   Tags                        list  [10, 20]
//   Weekly                      vect  3 elements
//
// Depth-first pre-order, which is what P2PmsgRecurs produces, is therefore
//   Company Product Version Build Founded Notes Memo Tags Weekly
//
static void BuildTree(P2PmsgMgr& oMgr)
{
    oMgr.r_Desc(P3PmsgField::AttrCMD_Create);

    // The fields, by name. A P2PmsgMgr is a P3PmsgField that the caller owns,
    // so it holds still and binds a view directly. The nested levels are its
    // named children, bound with MsgFieldAnchor::Child -- found afresh by name
    // on every access, never the parent's CURSOR item that SelectItem hands
    // out. Write order is tree order: a parent's value is written before its
    // children, as the long-hand calls did.
    MsgFieldAnchor atCompany = MsgFieldAnchor::Child(oMgr, L"Company");
    MsgFieldAnchor atProduct = MsgFieldAnchor::Child(atCompany, L"Product");

    MsgViewOf<StoreView>   store  (oMgr);
    MsgViewOf<CompanyView> company(atCompany);
    MsgViewOf<ProductView> product(atProduct);

    store->Company   = L"Ivyware";
    company->Product = L"Chartboard";
    product->Version = L"3.1";
    product->Build   = 4210;
    company->Founded = 1996;

    store->Notes = L"misc";
    Field(oMgr, L"Notes")[L"Memo"] = L"hello";      // a one-off: the dynamic form

    // Lists and vects have no named-field form: their elements are cells
    // addressed by position, so they stay plain.
    P3PmsgList oTags(L"Tags", P3PmsgData((int)0));
    oTags += P3PmsgData((int)10);
    oTags += P3PmsgData((int)20);
    oMgr.r_Desc() += oTags;

    P3PmsgVect oWeekly(3, L"Weekly", P3PmsgData((int)0));
    for (int i = 0; i < 3; i++)
        oWeekly.r_data(i).c_int((i + 1) * 100);
    oMgr.r_Desc() += oWeekly;
}

// The walk itself, in full. This is the whole point of the class: one loop,
// no recursion in the caller, an arbitrary depth of tree.
static bool WalkWholeTree(P3PmsgItem& oRoot, std::vector<std::wstring>& vOut)
{
    P2PmsgRecurs oRec(oRoot);
    int nSteps = 0;

    while (!oRec.IsEoRecurs())
    {
        if (++nSteps > kMaxSteps)
            return false;

        vOut.push_back(oRec.c_wstr());

        // Descend only into plain fields. Push() on a list or a vect throws
        // "Attempt to push non-P2PmsgItem environment" -- their elements are
        // cells and elements, not named children, so there is nothing below
        // them for a cursor chain to stand on.
        if (oRec.IsField())
            oRec.Push();

        ++oRec;
    }
    return true;
}

static std::wstring Join(const std::vector<std::wstring>& v)
{
    std::wstring s;
    for (size_t i = 0; i < v.size(); i++)
    {
        if (i) s += L" ";
        s += v[i];
    }
    return s;
}


// =========================================================================
// 1. P2PmsgRecurs -- one loop, the whole subtree
// =========================================================================
//
// Construction takes the item whose DESCENDANTS are to be walked, so the
// root itself is not visited -- exactly like P3PmsgCurs, which the class is
// built on. The cursor starts at element 0, so the first position is already
// live: visit, THEN advance.
//
static void Demo_RecursWalk()
{
    Section(L"1. P2PmsgRecurs -- one flat loop over a whole subtree");

    P2PmsgMgr oMgr(VBLock_Addr64, 8192, 1u << 20);
    CHECK(oMgr.IsValid());
    BuildTree(oMgr);

    std::vector<std::wstring> vSeen;
    CHECK(WalkWholeTree(oMgr, vSeen));

    wprintf(L"  visited : %s\n", Join(vSeen).c_str());

    CHECK(vSeen.size() == 9);
    if (vSeen.size() == 9)
    {
        CHECK(vSeen[0] == L"Company");
        CHECK(vSeen[1] == L"Product");
        CHECK(vSeen[2] == L"Version");   // depth 3 -- two Pushes below the root
        CHECK(vSeen[3] == L"Build");
        CHECK(vSeen[4] == L"Founded");   // back up one level, on its own
        CHECK(vSeen[5] == L"Notes");     // back up two
        CHECK(vSeen[6] == L"Memo");
        CHECK(vSeen[7] == L"Tags");
        CHECK(vSeen[8] == L"Weekly");
    }

    // The unwinding is the part worth noticing. Nothing in the loop pops:
    // operator++ finds the deepest cursor, sees it is spent, pops it and
    // retries -- so "Founded" and "Notes" cost the caller nothing.

    // A one-level walk over the same tree sees only the four top-level names,
    // which is the difference the class exists for.
    P3PmsgCurs& oCurs = oMgr.r_Desc().r_Curs();
    int nTop = 0;
    for (int i = 0; oCurs.Goto(i); i++)
        ++nTop;
    CHECK(nTop == 4);
    CHECK((int)vSeen.size() > nTop);
    wprintf(L"  one level: %d names, whole subtree: %d\n", nTop, (int)vSeen.size());

    // The tree the views built is the tree the long-hand calls built: the
    // same names, and the plain accessors read it unchanged.
    CHECK(oMgr.SelectItem(L"Company").SelectItem(L"Product").SelectItem(L"Build").c_int() == 4210);
    CHECK(Field(oMgr, L"Company")[L"Product"][L"Build"].AsInt() == 4210);
    CHECK(wcscmp(oMgr.SelectItem(L"Notes").SelectItem(L"Memo").c_wstr(), L"hello") == 0);
}


// =========================================================================
// 2. Push, Pop and Break -- the caller drives the descent
// =========================================================================
//
// Push() returns the level it just entered, counting from 1. Pop() returns
// the level it came back to. Break() abandons the whole walk: it pops every
// pushed level and runs the top cursor off its end, so IsEoRecurs() is true
// on the next test.
//
static void Demo_PushPopBreak()
{
    Section(L"2. Push / Pop / Break -- driving the descent by hand");

    P2PmsgMgr oMgr(VBLock_Addr64, 8192, 1u << 20);
    BuildTree(oMgr);

    // Not descending at all reduces it to a one-level cursor.
    {
        P2PmsgRecurs oRec(oMgr);
        std::vector<std::wstring> v;
        int nSteps = 0;
        while (!oRec.IsEoRecurs() && ++nSteps <= kMaxSteps)
        {
            v.push_back(oRec.c_wstr());
            ++oRec;                       // no Push -- stay on the top level
        }
        CHECK(v.size() == 4);
        CHECK(Join(v) == L"Company Notes Tags Weekly");
    }

    // Push reports the level entered; Pop reports the level returned to.
    {
        P2PmsgRecurs oRec(oMgr);
        CHECK(wcscmp(oRec.c_wstr(), L"Company") == 0);
        CHECK(oRec.IsField());

        const int nLevel = oRec.Push();
        CHECK(nLevel == 1);
        ++oRec;                            // lands on Company's first child
        CHECK(wcscmp(oRec.c_wstr(), L"Product") == 0);

        const int nInner = oRec.Push();
        CHECK(nInner == 2);
        ++oRec;
        CHECK(wcscmp(oRec.c_wstr(), L"Version") == 0);

        // Pop abandons the rest of Product's children and returns to the
        // level that pushed. It always pops the DEEPEST open level, however
        // deep the chain, so this is the one on Product.
        oRec.Pop();
        CHECK(wcscmp(oRec.c_wstr(), L"Product") == 0);   // back where we were
    }

    // Push on a non-field throws rather than silently doing nothing.
    {
        P2PmsgRecurs oRec(oMgr);
        std::vector<std::wstring> v;
        int nSteps = 0;
        while (!oRec.IsEoRecurs() && ++nSteps <= kMaxSteps)
        {
            if (wcscmp(oRec.c_wstr(), L"Tags") == 0) break;
            ++oRec;
        }
        CHECK(wcscmp(oRec.c_wstr(), L"Tags") == 0);
        CHECK(oRec.IsList());
        CHECK(!oRec.IsField());

        bool bThrew = false;
        try                    { oRec.Push(); }
        catch (P2Pevent* pEVT) { bThrew = true; if (pEVT) pEVT->Cancel(false); }
        CHECK(bThrew);
    }

    // Break ends the walk wherever it is, at whatever depth.
    {
        P2PmsgRecurs oRec(oMgr);
        oRec.Push();
        ++oRec;
        oRec.Push();
        ++oRec;
        CHECK(!oRec.IsEoRecurs());
        oRec.Break();
        CHECK(oRec.IsEoRecurs());
    }
}


// =========================================================================
// 3. What the walker exposes at each stop
// =========================================================================
//
// Every accessor forwards down the chain to the deepest open cursor, so
// r_data()/r_name()/r_item() always describe the current position rather
// than the level the walk started on.
//
static void Demo_RecursExposure()
{
    Section(L"3. Reading the tree through the walker");

    P2PmsgMgr oMgr(VBLock_Addr64, 8192, 1u << 20);
    BuildTree(oMgr);

    int nFields = 0, nLists = 0, nVects = 0, nSteps = 0;
    int nBuild = 0;
    std::wstring strVersion;
    int nBuildByName = 0, nFounded = 0;
    std::wstring strVersionByName;

    P2PmsgRecurs oRec(oMgr);
    while (!oRec.IsEoRecurs() && ++nSteps <= kMaxSteps)
    {
        if      (oRec.IsList()) ++nLists;
        else if (oRec.IsVect()) ++nVects;
        else if (oRec.IsField())
        {
            ++nFields;

            // r_data() is the current node's cell. Ask the type before
            // reading it -- the tree is heterogeneous by design. This is the
            // walker's own exposure, so it stays plain: the cell under the
            // walker has no name to look up, it IS the position.
            if (wcscmp(oRec.c_wstr(), L"Build") == 0)
                nBuild = oRec.r_data().c_int();
            if (wcscmp(oRec.c_wstr(), L"Version") == 0)
                strVersion = oRec.r_data().c_wstr();

            // Standing on a node, its CHILDREN are named fields, so a view
            // reads them. oRec.r_item() is the walker's own cursor item: it
            // holds still for the whole stop and only moves on ++, so it may
            // anchor a view that lives no longer than this stop.
            if (wcscmp(oRec.c_wstr(), L"Product") == 0)
            {
                MsgViewOf<ProductView> here(oRec.r_item());
                nBuildByName     = here->Build;
                strVersionByName = here->Version;
            }
            if (wcscmp(oRec.c_wstr(), L"Company") == 0)
            {
                MsgViewOf<CompanyView> here(oRec.r_item());
                nFounded = here->Founded;
            }

            oRec.Push();
        }
        ++oRec;
    }

    CHECK(nFields == 7);        // Company Product Version Build Founded Notes Memo
    CHECK(nLists  == 1);        // Tags
    CHECK(nVects  == 1);        // Weekly
    CHECK(nBuild  == 4210);     // read three levels down
    CHECK(strVersion == L"3.1");

    // The by-name reads, taken one level ABOVE the walker's own reads, agree
    // with them -- and reading did not knock the walk off course (the counts
    // above are the same counts as in the long-hand harness).
    CHECK(nBuildByName == nBuild);
    CHECK(strVersionByName == strVersion);
    CHECK(nFounded == 1996);
    wprintf(L"  %d fields, %d list, %d vect; Build=%d Version=%s\n",
            nFields, nLists, nVects, nBuild, strVersion.c_str());
}


// =========================================================================
// 4. P3PmsgTime -- the TIME64 cell
// =========================================================================
//
// The only typed subclass of P3PmsgData. It stores a 64-bit time_t -- the
// value CTime carries -- under its own type tag, VBLockData_TIME64, so a
// consumer can tell a timestamp from a plain 64-bit integer.
//
// Entirely plain in the field-access port: every cell here is a free-standing
// P3PmsgData with no name, and the field layer reaches only NAMED fields. A
// named time is section 5's MsgTime.
//
static void Demo_Time()
{
    Section(L"4. P3PmsgTime -- a cell that is known to be a time");

    // A fixed instant, so the harness is deterministic.
    const CTime    oWhen(2026, 8, 9, 14, 30, 0);
    const __int64  iWhen = oWhen.GetTime();

    P3PmsgTime oTime(iWhen);
    CHECK(oTime.DataType() == VBLockData_TIME64);
    CHECK(!oTime.IsNull());               // constructed FROM a value

    // The type tag is what distinguishes it from an INT64 holding the same
    // number -- same bits, different meaning.
    P3PmsgData oPlain((__int64)iWhen);
    CHECK(oPlain.DataType() == VBLockData_INT64);
    CHECK(oTime.DataType() != oPlain.DataType());

    // c_time64() reads it back. It is the reader for BOTH 64-bit tags --
    // there is no c_int64 -- so it accepts INT64 and TIME64 alike.
    CHECK(oTime.c_time64() == iWhen);
    CHECK(oPlain.c_time64() == iWhen);

    // ToString renders TIME64 as a calendar timestamp rather than a number,
    // which is the visible payoff of carrying the tag.
    const CString strShown = oTime.ToString();
    wprintf(L"  ToString: %s\n", (LPCWSTR)strShown);
    CHECK(strShown.Find(L"2026-08-09") == 0);
    CHECK(strShown.Find(L"14:30:00") > 0);

    // Default construction is a NULL time of the right type: the tag says
    // what the cell is FOR, the NULL attribute says it has no value yet.
    {
        P3PmsgTime oNull;
        CHECK(oNull.DataType() == VBLockData_TIME64);
        CHECK(oNull.IsNull());
        CHECK(CString(oNull.ToString()) == L"Null");
    }

    // Copy and assignment go through P3PmsgData, so the tag travels -- and
    // the copy is INDEPENDENT. Both were true only after P3PmsgData gained a
    // copy constructor; the compiler-generated one shared the source's cell
    // and destroyed it twice.
    {
        P3PmsgTime oCopy(oTime);
        CHECK(oCopy.DataType() == VBLockData_TIME64);
        CHECK(oCopy.c_time64() == iWhen);
        CHECK(oTime == oCopy);

        oCopy.c_time64(iWhen + 3600);          // mutate the copy
        CHECK(oCopy.c_time64() == iWhen + 3600);
        CHECK(oTime.c_time64() == iWhen);         // source untouched
        CHECK(oTime != oCopy);
    }
    CHECK(oTime.c_time64() == iWhen);             // and still intact after it dies

    {
        P3PmsgTime oAssigned;
        CHECK(oAssigned.IsNull());
        oAssigned = oTime;
        CHECK(oAssigned.DataType() == VBLockData_TIME64);
        CHECK(oAssigned.c_time64() == iWhen);
        CHECK(!oAssigned.IsNull());               // the attribute copies too
    }
}


// =========================================================================
// 5. A time in a tree, and across Save/Load
// =========================================================================
//
// A P3PmsgTime is a P3PmsgData, so it goes into a field like any other cell
// and the tag is part of the heap image -- which is what makes a stored
// timestamp still a timestamp after a round trip to disk.
//
// In the field-access port every field of the record goes in by name through
// a typed view: Label; Created, a MsgTime, which is stored as TIME64 exactly
// as P3PmsgTime would store it; and Stamp, the SAME instant as a plain int64.
// MsgTime is its own type so that a long long stays an integer, and each
// reader wants its own type: AsTime() reads Created, AsInt64() refuses it.
//
struct Record : MsgView
{
    MSG_FIELD ( Label,   std::wstring );
    MSG_FIELD ( Created, MsgTime );       // TIME64
    MSG_FIELD ( Stamp,   long long );     // a time_t, but tagged INT64
};

static void Demo_TimeInTree()
{
    Section(L"5. A TIME64 cell in a store, and through Save/Load");

    WCHAR szDir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, szDir);
    CString strPath;
    strPath.Format(L"%sRecursTimeTest_%lu.p2p", szDir, GetCurrentProcessId());

    const CTime   oWhen(2026, 8, 9, 14, 30, 0);
    const __int64 iWhen = oWhen.GetTime();

    {
        P2PmsgMgr oMgr(VBLock_Addr64, 4096, 1u << 20);
        oMgr.r_Desc(P3PmsgField::AttrCMD_Create);

        MsgViewOf<Record> rec(oMgr);
        rec->Created = MsgTime(iWhen);          // was DeclareItem(L"Created", P3PmsgTime(iWhen))
        rec->Label   = L"a record";
        rec->Stamp   = (long long)iWhen;

        {
            P3PmsgField& oBack = oMgr.SelectItem(L"Created");
            CHECK(oBack.DataType() == VBLockData_TIME64);
            CHECK(oBack.c_time64() == iWhen);
        }

        // The same instant two ways, and the tag is the difference.
        CHECK(Field(oMgr, L"Created").DataType() == VBLockData_TIME64);
        CHECK(Field(oMgr, L"Stamp").DataType() == VBLockData_INT64);
        CHECK(rec->Stamp.Get() == (long long)iWhen);
        CHECK(oMgr.SelectItem(L"Stamp").c_time64() == iWhen);   // the long-hand agrees
        CHECK(rec->Created.Get() == MsgTime(iWhen));
        CHECK(Refused([&] { (void)Field(oMgr, L"Created").AsInt64(); }));
        CHECK(Refused([&] { (void)Field(oMgr, L"Stamp").AsTime(); }));

        oMgr.Save(strPath, true);
    }

    {
        P2PmsgMgr oLoaded(strPath);
        CHECK(oLoaded.IsValid());

        {
            P3PmsgField& oBack = oLoaded.SelectItem(L"Created");
            CHECK(oBack.DataType() == VBLockData_TIME64);   // the tag survived
            CHECK(oBack.c_time64() == iWhen);               // and so did the value
            wprintf(L"  reloaded: %s\n", (LPCWSTR)oBack.ToString());
        }

        // The by-name fields survived with it.
        MsgViewOf<Record> rec(oLoaded);
        CHECK(rec->Label.Get() == L"a record");
        CHECK(rec->Stamp.Get() == (long long)iWhen);
        CHECK(rec->Created.Get().Seconds() == iWhen);
        CHECK(Field(oLoaded, L"Stamp").DataType() == VBLockData_INT64);
    }

    _wremove(strPath);
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

    wprintf(L"=== RecursTimeTest - recursive walking and TIME64 cells ===\n");
    fflush(stdout);

    try
    {
        Demo_RecursWalk();
        Demo_PushPopBreak();
        Demo_RecursExposure();
        Demo_Time();
        Demo_TimeInTree();
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
