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
// DataFieldTest.cpp
//
// The Msgcore DATA MODEL, from the bottom up. No networking, no Targetcore --
// this harness links Msgcore.lib alone and touches nothing else.
//
// Msgcore is the message *content* library: a self-describing tree of named,
// typed cells packed into a relocation-safe heap. Targetcore moves those trees
// between hubs; everything about what a message CONTAINS lives here.
//
// The four ideas, in the order they build on each other:
//
//   P3PmsgData   a typed value cell     -- an int, a double, a wide string, a blob
//   P3PmsgName   a bounded name         -- 63 UTF-16 units, stored inline
//   P3PmsgField  name + data            -- and, optionally, three side-cars:
//                  .r_Desc()              its DESCENDANTS (the child tree)
//                  .r_Attr()              its ATTRIBUTES  (a parallel @-keyed tree)
//                  .r_Stck()              its value STACK (push / mutate / pop)
//   P2Pevent     the error channel      -- Msgcore throws P2Pevent*, not std::exception
//
// P3PmsgItem is a typedef of P3PmsgField (P2Pmsg.h:746). "Field" and "item" are
// the same type; the two names only signal intent -- leaf vs interior node.
//
// Verdict is reported by process EXIT CODE (unambiguous for a headless run):
//   0 = SUCCESS : every check passed.
//   2 = ASSERT  : an MFC/CRT assertion fired (banner printed).
//   3 = FAIL    : at least one check failed; the first failing line is printed.

#include "stdafx.h"
#include "DataFieldTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pmsg.h"
#include "MsgAttr.h"
#include "MsgDesc.h"
#include "MsgStck.h"
#include "Msgexception.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// MFC requires exactly one CWinApp instance per executable. Msgcore's public
// headers use CString/CList, so even a console harness is an MFC client.
CWinApp theApp;

// -------------------------------------------------------------------------
// Minimal check harness (same exit-code contract as _Targetcore_UseExamples\DirectExamples).
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
        if (pnRet) *pnRet = 0;   // don't invoke the debugger
        ExitProcess(2);
    }
    return FALSE;
}


// =========================================================================
// 1. P3PmsgData -- the typed value cell
// =========================================================================
//
// A P3PmsgData is a discriminated value: a type tag plus storage sized for
// that type. The constructor picks the tag from the C++ type of the argument,
// and each c_XXX() accessor asserts the tag matches. There is no implicit
// numeric conversion -- c_int() on a DOUBLE cell throws a P2Pevent, it does
// not silently truncate. That strictness is the point: a message that crosses
// a wire keeps its declared shape.
//
static void Demo_TypedCells()
{
    Section(L"1. P3PmsgData -- typed value cells");

    // Each accessor returns a REFERENCE, so the cell is readable and writable
    // through the same call.
    P3PmsgData oInt = (int)1;
    oInt.c_int(12345);
    CHECK(oInt.c_int() == 12345);

    P3PmsgData oShort = (short)1;
    oShort.c_short(7);
    CHECK(oShort.c_short() == 7);

    P3PmsgData oDouble = (double)1.0;
    oDouble.c_double(2.5);
    CHECK(oDouble.c_double() == 2.5);

    P3PmsgData oBool = true;
    CHECK(oBool.c_bool() == true);
    oBool.c_bool(false);
    CHECK(oBool.c_bool() == false);

    P3PmsgData oI64 = (INT64)0;
    oI64.c_int64(9007199254740993LL);          // > 2^53: survives as an integer
    CHECK(oI64.c_int64() == 9007199254740993LL);

    // Wide strings. The default storage for LPCWSTR is WSTR16 -- 16-bit units,
    // which is what the wire format and the Linux port both assume.
    P3PmsgData oStr = L"Hello, \x20ACuro world";   // includes U+20AC
    CHECK(oStr.c_wstr() != nullptr);
    CHECK(wcscmp(oStr.c_wstr(), L"Hello, \x20ACuro world") == 0);
    // c_size() reports the cell's stored size, which for a variable-width
    // WSTR16 is the allocated span -- not the character count.
    wprintf(L"  wstr cell   : \"%s\"  (c_size=%zu)\n", oStr.c_wstr(), oStr.c_size());

    // A blob is opaque bytes with a size -- no interpretation, no terminator.
    struct Sample { int nId; double dValue; };
    Sample oSample = { 42, 3.5 };
    P3PmsgData oBlob = DataBLOB16(oSample);
    Sample* pBack = (Sample*)oBlob.c_vBlob();
    CHECK(pBack != nullptr && pBack->nId == 42 && pBack->dValue == 3.5);

    // Assignment copies the value AND the type tag.
    P3PmsgData oCopy;
    oCopy = oInt;
    CHECK(oCopy.c_int() == 12345);
    CHECK(oCopy.DataType() == oInt.DataType());

    // ToStringType() names the tag; ToString() renders the value. Both are
    // how the FileSystem layer writes a node's ".type" and "value" files.
    wprintf(L"  int cell    : type=%s value=%s\n",
            oInt.ToStringType(), oInt.ToString());
    wprintf(L"  double cell : type=%s value=%s\n",
            oDouble.ToStringType(), oDouble.ToString());
}


// =========================================================================
// 2. P3PmsgField -- name + data
// =========================================================================
//
// A field is a P3PmsgName and a P3PmsgData glued together by inheritance
// (P2Pmsg.h:589 -- P3PmsgField : public P3PmsgName, public P3PmsgData). That
// is why oField.c_int() and oField == L"Name" both work directly on it.
//
// The one rule worth internalising: a name-only field has NO data cell yet.
// Assigning a P3PmsgData establishes one; only then may c_int() write.
//
static void Demo_NameAndData()
{
    Section(L"2. P3PmsgField -- a named, typed cell");

    P3PmsgField oField(L"Johnno");
    CHECK(oField == L"Johnno");
    CHECK(!(oField == L"Somebody"));

    // Give it a cell. DataType() changes because the field went from
    // "no data" to "INT32".
    UCHAR uBefore = oField.DataType();
    oField = P3PmsgData((int)1);
    UCHAR uAfter  = oField.DataType();
    CHECK(uBefore != uAfter);
    oField.c_int(42);
    CHECK(oField.c_int() == 42);

    // Assigning a NAME renames in place -- the data cell survives untouched.
    // (Assigning DATA would have replaced the cell instead.)
    oField = P3PmsgName(L"Larry");
    CHECK(oField == L"Larry");
    CHECK(oField.c_int() == 42);
    wprintf(L"  renamed to '%s', value still %d\n", oField.r_name().c_name(), oField.c_int());

    // Copy construction is a deep copy of both halves.
    P3PmsgField oCopy = oField;
    oCopy.AssertValid();
    CHECK(oCopy.c_int() == 42);
    oCopy.c_int(99);
    CHECK(oField.c_int() == 42);           // independent storage
}


// =========================================================================
// 3. Descendants -- the child tree
// =========================================================================
//
// r_Desc() is a field's child collection. DeclareItem() creates the collection
// on demand, so building a tree is just a chain of declares. Every interior
// node is itself an ordinary P3PmsgField, which is what makes the model
// uniform: there is no separate "node" type to learn.
//
static void Demo_Descendants()
{
    Section(L"3. Descendants -- building a tree");

    P3PmsgField oRoot(L"Order");

    // Leaves at the root.
    oRoot.DeclareItem(L"OrderId",  P3PmsgData((int)10045));
    oRoot.DeclareItem(L"Customer", P3PmsgData(L"Ivyware Pty Ltd"));
    oRoot.DeclareItem(L"Total",    P3PmsgData((double)1299.50));

    CHECK(oRoot.Exists(L"OrderId"));
    CHECK(oRoot.Exists(L"Customer"));
    CHECK(!oRoot.Exists(L"Missing"));
    CHECK(oRoot.SelectItem(L"OrderId").c_int() == 10045);
    CHECK(wcscmp(oRoot.SelectItem(L"Customer").c_wstr(), L"Ivyware Pty Ltd") == 0);
    CHECK(oRoot.SelectItem(L"Total").c_double() == 1299.50);

    // SelectItem returns a REFERENCE into the tree, so writes land in the tree.
    oRoot.SelectItem(L"Total").c_double(1350.00);
    CHECK(oRoot.SelectItem(L"Total").c_double() == 1350.00);

    // Nesting: declare into the child you just selected. Two levels down.
    P3PmsgField& oAddr = oRoot.DeclareItem(L"ShipTo", P3PmsgData(L""));
    oAddr.DeclareItem(L"City",     P3PmsgData(L"Melbourne"));
    oAddr.DeclareItem(L"Postcode", P3PmsgData((int)3000));
    CHECK(oRoot.SelectItem(L"ShipTo").Exists(L"City"));
    CHECK(wcscmp(oRoot.SelectItem(L"ShipTo").SelectItem(L"City").c_wstr(), L"Melbourne") == 0);
    CHECK(oRoot.SelectItem(L"ShipTo").SelectItem(L"Postcode").c_int() == 3000);

    // GetCount() on the descendant container; Delete() removes by name.
    CHECK((int)oRoot.r_Desc().GetCount() == 4);
    CHECK(oRoot.Delete(L"Total"));
    CHECK(!oRoot.Exists(L"Total"));
    CHECK((int)oRoot.r_Desc().GetCount() == 3);

    wprintf(L"  Order has %d children, ShipTo.City='%s'\n",
            (int)oRoot.r_Desc().GetCount(),
            oRoot.SelectItem(L"ShipTo").SelectItem(L"City").c_wstr());

    // bUpdate=TRUE re-declares in place instead of adding a duplicate name.
    oRoot.DeclareItem(L"OrderId", P3PmsgData((int)20090), TRUE);
    CHECK(oRoot.SelectItem(L"OrderId").c_int() == 20090);
    CHECK((int)oRoot.r_Desc().GetCount() == 3);

    // Truncate empties the whole child collection.
    P3PmsgField oScratch(L"Scratch");
    oScratch.DeclareItem(L"a", P3PmsgData((int)1));
    oScratch.DeclareItem(L"b", P3PmsgData((int)2));
    CHECK((int)oScratch.r_Desc().GetCount() == 2);
    oScratch.Truncate();
    CHECK((int)oScratch.r_Desc().GetCount() == 0);
}


// =========================================================================
// 4. Attributes -- the parallel tree
// =========================================================================
//
// r_Attr() is a SECOND child collection hanging off the same field, addressed
// separately from r_Desc(). Use it for metadata that should not appear when
// something walks the content tree: units, permissions, provenance, a cache
// marker. The TreeFs layer surfaces this collection as ".attr/" and as POSIX
// xattrs, precisely because it is out-of-band from the data.
//
// Unlike descendants, the collection is NOT created on demand -- you must ask
// for it with AttrCMD_Create the first time.
//
static void Demo_Attributes()
{
    Section(L"4. Attributes -- metadata beside the value");

    P3PmsgField oTemp(L"Temperature", P3PmsgData((double)21.5));
    CHECK(!oTemp.IsAttributed());

    oTemp.r_Attr(P3PmsgField::AttrCMD_Create) += P3PmsgField(L"Unit",   P3PmsgData(L"Celsius"));
    oTemp.r_Attr()                            += P3PmsgField(L"Sensor", P3PmsgData((int)7));
    oTemp.AssertValid();

    CHECK(oTemp.IsAttributed());
    CHECK(oTemp.r_Attr().Exists(L"Unit"));
    CHECK(oTemp.r_Attr().Exists(L"Sensor"));
    CHECK(!oTemp.r_Attr().Exists(L"Nobody"));
    CHECK((int)oTemp.r_Attr().GetCount() == 2);

    // The value itself is untouched by any of that.
    CHECK(oTemp.c_double() == 21.5);

    // Attributes and descendants are independent collections on one field.
    oTemp.DeclareItem(L"Reading", P3PmsgData((double)21.5));
    CHECK(oTemp.IsDescendant());
    CHECK((int)oTemp.r_Desc().GetCount() == 1);
    CHECK((int)oTemp.r_Attr().GetCount() == 2);   // still 2 -- separate trees

    wprintf(L"  Temperature=%.1f  @Unit='%s'  @Sensor=%d\n",
            oTemp.c_double(),
            oTemp.r_Attr().SelectItem(L"Unit").c_wstr(),
            oTemp.r_Attr().SelectItem(L"Sensor").c_int());
}


// =========================================================================
// 5. MsgStck -- the per-field value stack
// =========================================================================
//
// Push() saves the field's current name+data inside the field itself; Pop()
// restores it. It is a scoped override: mutate freely, then unwind. Handy for
// a speculative edit you may want to abandon without holding a separate copy.
//
static void Demo_Stack()
{
    Section(L"5. MsgStck -- push / mutate / pop");

    P3PmsgField oField(L"Larry", DataBSTR08(L"Data"));
    CHECK(!oField.IsStacked());

    oField.r_Stck().Push();
    CHECK(oField.IsStacked());

    oField = P3PmsgName(L"Larry-Pushed");
    CHECK(oField == L"Larry-Pushed");

    if (oField.IsStacked())
        oField.r_Stck().Pop();
    oField.AssertValid();
    CHECK(oField == L"Larry");                     // the override unwound
    wprintf(L"  name after pop: '%s'\n", oField.r_name().c_name());
}


// =========================================================================
// 6. P2Pevent -- how Msgcore reports failure
// =========================================================================
//
// Msgcore does not return error codes and does not throw std::exception. It
// throws a POINTER to a P2Pevent, built fluently and carrying module, message,
// group and advice. A caller that catches one MUST dispose of it:
//
//   Cancel(true)   report it through the sink (may display on Windows)
//   Cancel(false)  discard silently -- what a test or a headless host wants
//   Isolate()      detach from the thread chain so it can be carried elsewhere
//
// The name bound below is a real, load-bearing limit: P3PmsgName stores 63
// UTF-16 UNITS inline. An overrun throws BEFORE writing, so the live name is
// left intact -- that is what makes the failure safe to catch and continue.
//
static void Demo_Events()
{
    Section(L"6. P2Pevent -- the error channel");

    // Build one by hand and read it back.
    P2Pevent* pEVT = EVERR->Module("DataFieldTest")
                          ->Message("Something went wrong: %d", 42)
                          ->Group("Demo")
                          ->Advice("This event was built, not thrown");
    CHECK(pEVT != nullptr);
    if (pEVT)
    {
        // The getters return CString; take LPCWSTR views before formatting,
        // else every %s and every comparison is an ambiguous overload.
        const CString strModule  = pEVT->GetModule();
        const CString strGroup   = pEVT->GetGroup();
        const CString strMessage = pEVT->GetMessage();

        CHECK(wcscmp((LPCWSTR)strGroup, L"Demo") == 0);
        CHECK(strMessage.GetLength() > 0);
        wprintf(L"  built event : module='%s' group='%s'\n  message     : %s\n",
                (LPCWSTR)strModule, (LPCWSTR)strGroup, (LPCWSTR)strMessage);
        pEVT->Cancel(false);            // discard silently
    }

    // Now provoke a real throw and recover from it.
    P3PmsgName oName(L"keep");          // 4 units
    bool bThrew = false;
    try
    {
        std::wstring strTooLong(64, L'a');           // 64 units -- one over
        oName.c_name(strTooLong.c_str(), 0);
    }
    catch (P2Pevent* pCaught)
    {
        bThrew = true;
        const CString strMessage = pCaught ? pCaught->GetMessage() : CString(L"<null>");
        wprintf(L"  caught      : %s\n", (LPCWSTR)strMessage);
        if (pCaught) pCaught->Cancel(false);
    }
    CHECK(bThrew);
    CHECK(oName == L"keep");            // rejected write left the name intact
    CHECK(oName.c_size() == 4);

    // 63 units is in bounds. The bound counts UTF-16 units, not code points:
    // one astral code point costs two.
    P3PmsgName oOk(L"seed");
    oOk.c_name(std::wstring(63, L'a').c_str(), 0);
    CHECK(oOk.c_size() == 63);

    std::wstring strAstral;
    for (int i = 0; i < 31; ++i) strAstral += L"\U0001F680";
    P3PmsgName oAstral(L"seed");
    oAstral.c_name(strAstral.c_str(), 0);
    CHECK(oAstral.c_size() == 62);      // 31 code points, 62 units
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

    wprintf(L"=== DataFieldTest - the Msgcore data model (no networking) ===\n");
    fflush(stdout);

    try
    {
        Demo_TypedCells();
        Demo_NameAndData();
        Demo_Descendants();
        Demo_Attributes();
        Demo_Stack();
        Demo_Events();
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
