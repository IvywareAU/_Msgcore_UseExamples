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
// ListVectTest.cpp
//
// The Msgcore CONTAINERS: P3PmsgList, P3PmsgVect, and the P3PmsgCurs cursor
// that walks a collection without knowing what is in it. No networking.
//
// Both containers are themselves P3PmsgField subclasses, so a list or a vect
// has a name, can be dropped into another field's descendants, and can nest
// inside another container. That uniformity is the whole trick: the tree has
// exactly one node type, and "list" / "vect" are shapes it can take.
//
//   P3PmsgList   a linked sequence of DATA CELLS, walked by opaque position.
//                Cheap to grow at either end; no random access.
//                Elements are P3PmsgData -- values, not named fields.
//
//   P3PmsgVect   a dense, indexed array of FIELDS. Random access by int.
//                32 inline slots (aAlloc[32]); past that it spills into
//                aExtra continuation blocks, transparently.
//
//   P3PmsgCurs   a positional cursor over a descendant or attribute
//                collection. Ask it IsItem/IsList/IsVect, then take the
//                matching reference. This is how generic code -- a printer,
//                a serialiser, a filesystem -- walks a tree it did not build.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 2 = ASSERT, 3 = at least one check failed.

#include "stdafx.h"
#include "ListVectTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pmsg.h"
#include "MsgList.h"
#include "MsgVect.h"
#include "MsgDesc.h"
#include "MsgCurs.h"
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
// 1. P3PmsgList -- a sequence of data cells
// =========================================================================
//
// Growth is at head or tail; traversal is by VBLaddr position, the same
// GetHeadPos/GetNext shape MFC's CList uses. GetNext ADVANCES the position
// and returns a reference to the element it just stepped over, so the loop
// condition is the position, not an index.
//
static void Demo_List()
{
    Section(L"1. P3PmsgList -- an ordered sequence of data cells");

    P3PmsgList oList;
    CHECK((int)oList.GetCount() == 0);

    oList.AddListTail(P3PmsgData((int)10));
    oList.AddListTail(P3PmsgData((int)20));
    oList.AddListTail(P3PmsgData((int)30));
    CHECK((int)oList.GetCount() == 3);

    // operator+= is AddListTail spelt shorter.
    oList += P3PmsgData((int)40);
    CHECK((int)oList.GetCount() == 4);

    // Forward walk.
    int nVisited = 0, nSum = 0;
    VBLaddr aPos = oList.GetHeadPos();
    while (aPos)
    {
        P3PmsgData& oData = oList.GetNext(aPos);
        nSum += oData.c_int();
        ++nVisited;
    }
    CHECK(nVisited == 4);
    CHECK(nSum == 100);
    wprintf(L"  forward : %d elements summing to %d\n", nVisited, nSum);

    // The tail is directly reachable -- GetTailPos() is the address the
    // forward walk would have finished on, and GetTail() the cell there.
    VBLaddr aLastSeen = 0, aScan = oList.GetHeadPos();
    while (aScan) { aLastSeen = aScan; oList.GetNext(aScan); }
    CHECK(oList.GetTailPos() == aLastSeen);
    CHECK(oList.GetTail().c_int() == 40);

    oList.DropTail();
    CHECK((int)oList.GetCount() == 3);
    CHECK(oList.GetTail().c_int() == 30);

    oList.DropHead();
    CHECK((int)oList.GetCount() == 2);
    aPos = oList.GetHeadPos();
    CHECK(oList.GetNext(aPos).c_int() == 20);

    // Growth at the head. AddListHead is AddListTail's mirror: it links the
    // new cell in front of GetHeadPos() rather than behind GetTailPos().
    oList.AddListHead(P3PmsgData((int)5));
    CHECK((int)oList.GetCount() == 3);
    aPos = oList.GetHeadPos();
    CHECK(oList.GetNext(aPos).c_int() == 5);      // list is now 5, 20, 30
    CHECK(oList.GetTail().c_int() == 30);

    // Backward walk. The items are doubly linked, so GetPrev mirrors GetNext
    // exactly: it STEPS BACK over the cell it returns, and the position falls
    // to 0 once the head has been handed out. Start from GetTailPos().
    int nBack = 0, nBackSum = 0, nFirstBack = 0;
    VBLaddr aRev = oList.GetTailPos();
    while (aRev)
    {
        P3PmsgData& oData = oList.GetPrev(aRev);
        if (nBack == 0) nFirstBack = oData.c_int();
        nBackSum += oData.c_int();
        ++nBack;
    }
    CHECK(nBack == 3);
    CHECK(nFirstBack == 30);                      // reverse order: 30, 20, 5
    CHECK(nBackSum == 55);                        // same cells as forward
    wprintf(L"  backward: %d elements summing to %d\n", nBack, nBackSum);

    // Restore the two-element list the rest of the section assumed.
    oList.DropHead();
    CHECK((int)oList.GetCount() == 2);

    // A list is heterogeneous -- each cell carries its own type tag.
    P3PmsgList oMixed;
    oMixed += P3PmsgData((int)7);
    oMixed += P3PmsgData((double)2.5);
    oMixed += P3PmsgData(L"seven and a half");
    CHECK((int)oMixed.GetCount() == 3);

    VBLaddr aMix = oMixed.GetHeadPos();
    CHECK(oMixed.GetNext(aMix).c_int() == 7);
    CHECK(oMixed.GetNext(aMix).c_double() == 2.5);
    CHECK(wcscmp(oMixed.GetNext(aMix).c_wstr(), L"seven and a half") == 0);

    oMixed.Truncate();
    CHECK((int)oMixed.GetCount() == 0);
}


// =========================================================================
// 2. P3PmsgVect -- an indexed array of fields
// =========================================================================
//
// The constructor takes an element count, a name for the vect, and a
// PROTOTYPE data cell that fixes each element's initial type. Elements are
// named fields (IsField(i) is true), addressed by r_data(i) / r_item(i).
//
static void Demo_Vect()
{
    Section(L"2. P3PmsgVect -- a dense, indexed array");

    P3PmsgVect oVect(3, L"Reading", P3PmsgData((int)0));
    CHECK((int)oVect.GetCount() == 3);
    CHECK(oVect.IsField(0));

    oVect.r_data(0).c_int(100);
    oVect.r_data(1).c_int(200);
    oVect.r_data(2).c_int(300);
    CHECK(oVect.r_data(0).c_int() == 100);
    CHECK(oVect.r_data(2).c_int() == 300);

    // Goto() is the bounds check: non-zero in range, 0 out of it. It never
    // throws, so it is the safe way to probe a length you do not trust.
    CHECK(oVect.Goto(0) != 0);
    CHECK(oVect.Goto(3) == 0);
    CHECK(oVect.Goto(-1) == 0);

    // Grow from empty. InsertAt(n) at the end appends; in the middle it
    // shifts everything right.
    P3PmsgVect oGrow(0, L"Elem", P3PmsgData((int)0));
    CHECK((int)oGrow.GetCount() == 0);
    oGrow.InsertAt(0, P3PmsgField(L"e", P3PmsgData((int)10)));   // [10]
    oGrow.InsertAt(1, P3PmsgField(L"e", P3PmsgData((int)30)));   // [10,30]
    oGrow.InsertAt(1, P3PmsgField(L"e", P3PmsgData((int)20)));   // [10,20,30]
    CHECK((int)oGrow.GetCount() == 3);
    CHECK(oGrow.r_data(0).c_int() == 10);
    CHECK(oGrow.r_data(1).c_int() == 20);
    CHECK(oGrow.r_data(2).c_int() == 30);

    // Delete compacts; an out-of-range delete is a false, not a throw.
    CHECK(oGrow.Delete(1));                                      // [10,30]
    CHECK((int)oGrow.GetCount() == 2);
    CHECK(oGrow.r_data(1).c_int() == 30);
    CHECK(!oGrow.Delete(9));

    // Copy assignment is a DEEP copy -- the two vects share no storage.
    P3PmsgVect oCopy(0, L"Copy", P3PmsgData((int)0));
    oCopy = oVect;
    CHECK((int)oCopy.GetCount() == 3);
    CHECK(oCopy.r_data(0).c_int() == 100);
    oCopy.r_data(0).c_int(999);
    CHECK(oVect.r_data(0).c_int() == 100);

    oCopy.Truncate();
    CHECK((int)oCopy.GetCount() == 0);
    wprintf(L"  vect of %d, deep copy is independent\n", (int)oVect.GetCount());
}


// =========================================================================
// 3. The 32-element seam
// =========================================================================
//
// A vect keeps its first 32 element addresses inline (aAlloc[32]) and spills
// the rest into aExtra continuation blocks. Nothing above the API sees the
// difference -- but an insert NEAR the seam has to shift elements across a
// block boundary, which is the interesting case and the one worth pinning in
// an example, because it is where an indexed container usually goes wrong.
//
static void Demo_VectSpill()
{
    Section(L"3. P3PmsgVect -- spilling past the 32 inline slots");

    P3PmsgVect oVect(0, L"Elem", P3PmsgData((int)0));
    for (int i = 0; i < 70; i++)
        oVect.InsertAt(i, P3PmsgField(L"e", P3PmsgData((int)(i * 10))));

    CHECK((int)oVect.GetCount() == 70);
    CHECK(oVect.r_data(0).c_int()  == 0);
    CHECK(oVect.r_data(31).c_int() == 310);   // last inline slot
    CHECK(oVect.r_data(32).c_int() == 320);   // first continuation slot
    CHECK(oVect.r_data(63).c_int() == 630);   // into the second block
    CHECK(oVect.r_data(69).c_int() == 690);

    // Insert exactly on the seam.
    oVect.InsertAt(32, P3PmsgField(L"e", P3PmsgData((int)9999)));
    CHECK((int)oVect.GetCount() == 71);
    CHECK(oVect.r_data(32).c_int() == 9999);
    CHECK(oVect.r_data(33).c_int() == 320);   // the former [32], pushed right
    CHECK(oVect.r_data(70).c_int() == 690);
    wprintf(L"  71 elements across 3 blocks, seam insert intact\n");
}


// =========================================================================
// 4. Nesting and containment
// =========================================================================
//
// A vect element may itself be a vect; a field's descendants may hold lists
// and vects alongside plain items. Every += is a deep copy into the
// container, so the source object stays independent afterwards.
//
static void Demo_Nesting()
{
    Section(L"4. Nesting -- containers inside containers");

    P3PmsgVect oInner(2, L"Inner", P3PmsgData((int)0));
    oInner.r_data(0).c_int(7);
    oInner.r_data(1).c_int(8);

    P3PmsgVect oOuter(0, L"Outer", P3PmsgData((int)0));
    oOuter.InsertAt(0, P3PmsgField(L"scalar", P3PmsgData((int)1)));
    oOuter.InsertAt(1, oInner);                      // element 1 IS a vect

    CHECK((int)oOuter.GetCount() == 2);
    CHECK(oOuter.IsField(0));
    CHECK(oOuter.IsVect(1));
    CHECK((int)oOuter.r_vect(1).GetCount() == 2);
    CHECK(oOuter.r_vect(1).r_data(1).c_int() == 8);

    // Deep copy: mutating the source does not reach the element.
    oInner.r_data(1).c_int(99);
    CHECK(oOuter.r_vect(1).r_data(1).c_int() == 8);

    // Drop a list and a vect into an ordinary field's descendants, then get
    // them back by name. r_Desc(AttrCMD_Create) makes the collection first.
    P3PmsgList oList(L"Samples", P3PmsgData((int)0));
    oList += P3PmsgData((int)11);
    oList += P3PmsgData((int)22);

    P3PmsgVect oPayload(3, L"Payload", P3PmsgData((int)0));
    oPayload.r_data(0).c_int(5);
    oPayload.r_data(1).c_int(6);
    oPayload.r_data(2).c_int(7);

    P3PmsgField oHolder(L"Holder");
    oHolder.r_Desc(P3PmsgField::AttrCMD_Create);
    oHolder.r_Desc() += oPayload;
    oHolder.r_Desc() += oList;
    oHolder.DeclareItem(L"Label", P3PmsgData(L"mixed bag"));

    CHECK(oHolder.r_Desc().Exists(L"Payload"));
    CHECK(oHolder.r_Desc().Exists(L"Samples"));
    CHECK((int)oHolder.r_Desc().GetCount() == 3);

    // SelectVect / SelectList return typed references into the tree.
    P3PmsgVect oBack(oHolder.r_Desc().SelectVect(L"Payload").r_Object());
    CHECK((int)oBack.GetCount() == 3);
    CHECK(oBack.r_data(0).c_int() == 5);
    CHECK(oBack.r_data(2).c_int() == 7);

    P3PmsgList& oListBack = oHolder.r_Desc().SelectList(L"Samples");
    CHECK((int)oListBack.GetCount() == 2);

    wprintf(L"  Holder holds a %d-vect, a %d-list and a plain item\n",
            (int)oBack.GetCount(), (int)oListBack.GetCount());
}


// =========================================================================
// 5. P3PmsgCurs -- walking a collection you did not build
// =========================================================================
//
// The idiom used throughout Msgcore itself (MsgAttr.cpp:485, MsgDesc.cpp:414):
//
//     P3PmsgCurs& oCurs = oDesc.r_Curs();
//     for (int i = 0; oCurs.Goto(i); i++) { ...ask what it is, then take it... }
//
// Goto() doubles as the loop condition -- it returns false past the end. The
// cursor is owned by the collection, so do not delete it and do not keep it
// across a structural change to the collection.
//
static void Demo_Cursor()
{
    Section(L"5. P3PmsgCurs -- generic traversal");

    // Build a deliberately mixed collection.
    P3PmsgField oRoot(L"Telemetry");
    oRoot.DeclareItem(L"Device",  P3PmsgData(L"sensor-04"));
    oRoot.DeclareItem(L"Uptime",  P3PmsgData((int)86400));

    P3PmsgList oHistory(L"History", P3PmsgData((int)0));
    oHistory += P3PmsgData((int)1);
    oHistory += P3PmsgData((int)2);
    oHistory += P3PmsgData((int)3);
    oRoot.r_Desc() += oHistory;

    P3PmsgVect oWindow(4, L"Window", P3PmsgData((double)0.0));
    for (int i = 0; i < 4; i++) oWindow.r_data(i).c_double(i * 1.5);
    oRoot.r_Desc() += oWindow;

    CHECK((int)oRoot.r_Desc().GetCount() == 4);

    int nItems = 0, nLists = 0, nVects = 0;

    P3PmsgCurs& oCurs = oRoot.r_Desc().r_Curs();
    for (int i = 0; oCurs.Goto(i); i++)
    {
        // Order matters: a list and a vect are both fields, so test the
        // specific shapes before the general one.
        if (oCurs.IsList())
        {
            ++nLists;
            P3PmsgList& oL = oCurs.r_list();
            wprintf(L"  [%d] LIST %-10s count=%d\n",
                    i, oL.r_name().c_name(), (int)oL.GetCount());
        }
        else if (oCurs.IsVect())
        {
            ++nVects;
            P3PmsgVect& oV = oCurs.r_vect();
            wprintf(L"  [%d] VECT %-10s count=%d\n",
                    i, oV.r_name().c_name(), (int)oV.GetCount());
        }
        else if (oCurs.IsItem())
        {
            ++nItems;
            P3PmsgItem& oI = oCurs.r_item();
            wprintf(L"  [%d] ITEM %-10s type=%s value=%s\n",
                    i, oI.r_name().c_name(), oI.ToStringType(), oI.ToString());
        }
    }

    CHECK(nItems == 2);
    CHECK(nLists == 1);
    CHECK(nVects == 1);
    CHECK(nItems + nLists + nVects == (int)oRoot.r_Desc().GetCount());
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

    wprintf(L"=== ListVectTest - Msgcore containers and cursors ===\n");
    fflush(stdout);

    try
    {
        Demo_List();
        Demo_Vect();
        Demo_VectSpill();
        Demo_Nesting();
        Demo_Cursor();
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
