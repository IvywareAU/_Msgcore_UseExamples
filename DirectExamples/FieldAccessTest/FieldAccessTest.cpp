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
// FieldAccessTest.cpp
//
// FIELD ACCESS BY NAME over a Msgcore tree -- Msgcore/MsgFieldRef.hpp, the
// header-only layer from MsgFieldAccessPlan.md. No networking.
//
// The plain API already reaches a field by name; what it costs is ceremony:
//
//     oRoot.DeclareItem ( L"uptime", P3PmsgData((int)86400), TRUE );
//     int up = oRoot.SelectItem ( L"uptime" ).c_int();
//
// This harness shows the two forms C++ allows instead, side by side with the
// plain call each one replaces:
//
//   Field ( item, L"name" ) = v     the DYNAMIC form, for a name known only at
//                                   run time; nests with [L"child"].
//
//   MsgViewOf<T> msg ( item );      a TYPED VIEW, for names known at compile
//   msg->name = v;                  time: a struct of MSG_FIELD members. A
//                                   member takes only its own type, so
//                                   `msg->uptime = L"x"` does not compile.
//
// `msg->unknownName` cannot be made to work in C++ -- there is no hook that
// turns an undeclared member into a lookup. That is what Field() is for.
//
// Five sections:
//   1. the dynamic form, every type, and what it wrote in plain-API terms
//   2. nesting -- a write creates the path, a read creates nothing
//   3. a typed view
//   4. the two codings -- Typed and Bytes -- and readers that accept both
//   5. what is refused, and how: an absent field, a wrong type, a long name
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 2 = ASSERT, 3 = at least one check failed.

#include "stdafx.h"
#include "FieldAccessTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pmsg.h"
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

// =========================================================================
// 1. The dynamic form
// =========================================================================
//
// One write per type. Each lands under its OWN tag, so the plain API reads
// it with the accessor it would always have used -- the proxy adds nothing to
// the tree that the long-hand calls would not have.
//
static void Demo_Dynamic()
{
    Section(L"1. Field(item, name) = value -- the dynamic form");

    P3PmsgField oRoot(L"Telemetry");

    Field(oRoot, L"device") = L"sensor-04";
    Field(oRoot, L"uptime") = 86400;
    Field(oRoot, L"serial") = 9000000000LL;
    Field(oRoot, L"ratio")  = 0.75;
    Field(oRoot, L"online") = true;
    Field(oRoot, L"note")   = "UTF-8 \xE2\x82\xAC";       // narrow = UTF-8, stored UTF-16
    const unsigned char raw[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    Field(oRoot, L"raw")    = MsgBlob(raw, sizeof raw);

    CHECK(Field(oRoot, L"device").AsText() == L"sensor-04");
    CHECK(Field(oRoot, L"uptime").AsInt() == 86400);
    CHECK(Field(oRoot, L"serial").AsInt64() == 9000000000LL);
    CHECK(Field(oRoot, L"ratio").AsReal() == 0.75);
    CHECK(Field(oRoot, L"online").AsBool());
    CHECK(Field(oRoot, L"note").AsText() == L"UTF-8 \x20AC");
    CHECK(Field(oRoot, L"raw").AsBlob() == MsgBlob(raw, sizeof raw));

    // The same fields, read the long way.
    CHECK(oRoot.SelectItem(L"uptime").c_int() == 86400);
    CHECK(wcscmp(oRoot.SelectItem(L"device").c_wstr(), L"sensor-04") == 0);
    CHECK(oRoot.SelectItem(L"note").r_data().DataType() == VBLockData_WSTR16);

    // A rewrite replaces the type as well as the value.
    Field(oRoot, L"uptime") = L"a long time";
    CHECK(Field(oRoot, L"uptime").DataType() == VBLockData_WSTR16);
    wprintf(L"  uptime is now text: \"%s\"\n", Field(oRoot, L"uptime").AsText().c_str());
}

// =========================================================================
// 2. Nesting
// =========================================================================
//
// [L"child"] extends the path. The write creates every item on the way; a
// read walks it and creates nothing, so asking does not change the answer.
//
static void Demo_Nesting()
{
    Section(L"2. Field(item, L\"pos\")[L\"x\"] -- nesting");

    P3PmsgField oRoot(L"Body");
    Field(oRoot, L"pos")[L"x"] = 1.5;
    Field(oRoot, L"pos")[L"y"] = -2.5;
    Field(oRoot, L"pos")[L"frame"][L"name"] = L"world";

    CHECK(Field(oRoot, L"pos")[L"x"].AsReal() == 1.5);
    CHECK(oRoot.SelectItem(L"pos").SelectItem(L"y").c_double() == -2.5);
    CHECK(Field(oRoot, L"pos")[L"frame"][L"name"].AsText() == L"world");

    MsgFieldRef ghost = Field(oRoot, L"vel")[L"x"];
    CHECK(!ghost.Exists());
    CHECK(!oRoot.Exists(L"vel"));              // the read created nothing

    // A ref holds NAMES. These three writes each move a cursor, and each ref
    // still finds its own field afterwards -- the reason a ref re-resolves
    // rather than keeping the item reference SelectItem hands back.
    MsgFieldRef px = Field(oRoot, L"pos")[L"x"];
    MsgFieldRef py = Field(oRoot, L"pos")[L"y"];
    px = 10.0; py = 20.0; px = 30.0;
    CHECK(px.AsReal() == 30.0 && py.AsReal() == 20.0);
}

// =========================================================================
// 3. A typed view
// =========================================================================
//
// MSG_FIELD takes the identifier once and makes both the member and its
// L"..." name, so the two cannot drift; a name past 63 UTF-16 units fails at
// compile time. msg->uptime = L"x" would stop at a static_assert reading
// "MSG_FIELD: this value's type is not the field's declared type".
//
struct Telemetry : MsgView
{
    MSG_FIELD ( device, std::wstring );
    MSG_FIELD ( uptime, int );
    MSG_FIELD ( ratio,  double );
    MSG_FIELD ( spare,  std::wstring );
};

static void Demo_View()
{
    Section(L"3. MsgViewOf<Telemetry> msg(item); msg->uptime = 86400 -- a typed view");

    P3PmsgField oRoot(L"Telemetry");
    MsgViewOf<Telemetry> msg(oRoot);

    msg->device = L"sensor-04";
    msg->uptime = 86400;
    msg->ratio  = 3;                          // an int into a double: same family
    msg->spare  = msg->device;                // field to field copies the VALUE

    int up = msg->uptime;                     // reads by conversion
    std::wstring dev = msg->device;
    CHECK(up == 86400);
    CHECK(dev == L"sensor-04");
    CHECK(msg->ratio.Get() == 3.0);
    CHECK(msg->spare.Get() == L"sensor-04");

    // The view's names ARE the field names -- the dynamic form sees them --
    // and the view hands out the dynamic form for what it does not declare.
    CHECK(Field(oRoot, L"uptime").AsInt() == 86400);
    msg[L"extra"] = 7;
    CHECK(msg[L"extra"].AsInt() == 7);

    CHECK(msg->spare.Erase());
    CHECK(!msg->spare.Exists());
    wprintf(L"  device=%s uptime=%d\n", dev.c_str(), up);
}

// =========================================================================
// 4. Two codings
// =========================================================================
//
// Typed (the default) stores each type under its own tag. Bytes stores every
// value as a blob of its native bytes -- the shape TargetFacade carries named
// fields in, which is what Targetcore's AppFields() uses so a direct client
// and a facade client read each other's fields. Readers accept either, so a
// reader never needs to know which a writer chose.
//
static void Demo_Codings()
{
    Section(L"4. MsgFieldCoding::Typed and ::Bytes");

    P3PmsgField oRoot(L"Mixed");
    MsgFieldAnchor typed = MsgFieldAnchor::Of(oRoot);
    MsgFieldAnchor bytes = MsgFieldAnchor::Of(oRoot, MsgFieldCoding::Bytes);

    Field(typed, L"t") = 86400;
    Field(bytes, L"b") = 86400;
    Field(bytes, L"s") = L"abc";

    CHECK(Field(typed, L"t").DataType() == VBLockData_INT32);
    CHECK(Field(typed, L"b").DataType() == VBLockData_BLOB16);
    CHECK(oRoot.SelectItem(L"b").r_data().c_size() == 4);       // an int is 4 bytes
    CHECK(oRoot.SelectItem(L"s").r_data().c_size() == 8);       // text keeps its NUL

    // Crossed: each coding's reader on the other's field.
    CHECK(Field(bytes, L"t").AsInt() == 86400);
    CHECK(Field(typed, L"b").AsInt() == 86400);
    CHECK(Field(typed, L"s").AsText() == L"abc");

    MsgViewOf<Telemetry> wire(oRoot, MsgFieldCoding::Bytes);
    wire->uptime = 42;
    CHECK(Field(typed, L"uptime").DataType() == VBLockData_BLOB16);
    CHECK((int)wire->uptime == 42);
}

// =========================================================================
// 5. What is refused
// =========================================================================
//
// Reads throw P2Pevent* exactly as c_int()/c_wstr() do: on an absent field,
// and on the wrong type. A name past 63 UTF-16 units is refused BEFORE the
// tree is touched, with a message that names the field. (Msgcore's own throw
// for it, from a plain DeclareItem, used to corrupt the heap; fixed
// 2026-10-02.)
//
static void Demo_Refusals()
{
    Section(L"5. What is refused");

    P3PmsgField oRoot(L"Strict");
    Field(oRoot, L"uptime") = 86400;

    CHECK(Refused([&] { (void)Field(oRoot, L"absent").AsInt(); }));
    CHECK(Refused([&] { (void)Field(oRoot, L"uptime").AsText(); }));
    CHECK(Refused([&] { (void)Field(oRoot, L"uptime").AsReal(); }));

    std::wstring n64(64, L'n');
    CHECK(Refused([&] { Field(oRoot, n64.c_str()) = 1; }));
    CHECK(!oRoot.Exists(n64.c_str()));

    Telemetry unbound;                                  // a view never given an item
    CHECK(Refused([&] { unbound.uptime = 1; }));
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

    wprintf(L"=== FieldAccessTest - field access by name (MsgFieldRef.hpp) ===\n");
    fflush(stdout);

    try
    {
        Demo_Dynamic();
        Demo_Nesting();
        Demo_View();
        Demo_Codings();
        Demo_Refusals();
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
