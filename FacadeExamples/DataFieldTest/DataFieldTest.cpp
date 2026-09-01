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
// DataFieldTest.cpp  (Light -- MsgFacade)
//
// The Msgcore DATA MODEL, from the bottom up -- through MsgFacade instead of
// through Msgcore's C++ classes. Same subject as
// ..\DirectExamples\DataFieldTest, same exit-code contract, and the same
// six sections in the same order, so the two files can be read side by side.
//
//   original (466 lines)                     light (this file)
//   ---------------------------------------------------------------------
//   stdafx.h + afx*.h + 5 Msgcore headers    #include "MsgFacadeFn.hpp"
//   CWinApp theApp                           (gone -- no MFC here)
//   _CrtSetReportHook assert trap            (gone -- no MFC assert to trap)
//   P3PmsgData oInt = (int)1                 root.declareInt(L"i32", 1)
//   oInt.c_int(12345                     node.set((long long)12345)
//   oInt.c_int()          (STRICT: throws)   node.asInt()   (any width, or
//                                                            MSGF_E_TYPE)
//   oField.DeclareItem(name, data)           node.declareText(name, value)
//   oField.r_Attr(AttrCMD_Create) += ...     node.declareText(name, v, Attr)
//   oField.r_Desc().GetCount()               node.count()  /  node.count(Attr)
//   catch (P2Pevent*) { p->Cancel(false)); }  catch (const msgf::Error& e)
//
// THE ONE STRUCTURAL DIFFERENCE, and it is worth understanding before reading
// on: the original built LOOSE OBJECTS. A `P3PmsgData oInt = (int)1;` is a
// value cell on the stack, owning its own little heap, belonging to no tree.
// The facade has no such thing on purpose -- every value it can name lives in
// a STORE, because a cell outside a store is exactly the object whose
// lifetime and copy semantics cost Msgcore a double-free bug (see the
// original tree's README, defect 5). So section 1 below declares its cells
// into a scratch store rather than on the stack, and everything else follows.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS : every check passed.
//   1 = SETUP   : the library or the store could not be created.
//   3 = FAIL    : at least one check failed; the failing lines are printed.

#include "MsgFacadeFn.hpp"
#include "LightHarness.h"

using namespace msgf;


// =========================================================================
// 1. Typed value cells
// =========================================================================
//
// Msgcore's cells are discriminated: a type tag plus storage sized for that
// tag. The original demonstrated this with P3PmsgData's constructor picking
// the tag from the C++ type of its argument, and each c_XXX() accessor
// ASSERTING that the tag matches -- c_int() on a DOUBLE throws.
//
// The facade keeps the storage strict and makes the READING tolerant, which
// is the third of its three reasons to exist: `type` says what to store at,
// and asInt() then reads the whole integer family at any width. Asking for
// something the cell does not hold is MSGF_E_TYPE -- an answer, not a throw.
//
static void Demo_TypedCells ( Store& st )
{
    light::Section ( L"1. typed value cells -- one store, every type" );

    Node root = st.root ( );
    Library lib;

    // Every integer width the model has, declared explicitly. The original
    // could only reach the widths P3PmsgData had a constructor for; UINT08 and
    // UINT16 have no accessor in the kernel AT ALL, so they were unreachable.
    struct { const wchar_t *name; unsigned char type; long long value; } aInts[] =
    {
      { L"i8",  MSGF_TYPE_INT08,  -100             },
      { L"u8",  MSGF_TYPE_UINT08,  200             },   // no c_* accessor exists
      { L"i16", MSGF_TYPE_INT16,  -30000           },
      { L"u16", MSGF_TYPE_UINT16,  60000           },   // nor for this one
      { L"i32", MSGF_TYPE_INT32,   12345           },
      { L"u32", MSGF_TYPE_UINT32,  4000000000      },
      { L"i64", MSGF_TYPE_INT64,   9007199254740993LL },  // > 2^53
      { L"u64", MSGF_TYPE_UINT64,  9007199254740993LL },
      { L"b",   MSGF_TYPE_BOOL,    1               },
    };

    for ( int i = 0; i < (int)( sizeof(aInts) / sizeof(aInts[0]) ); ++i )
    {
      root.declareInt ( aInts[i].name, aInts[i].value, Child, aInts[i].type );

      Node n = root.child ( aInts[i].name );
      CHECK ( n.type ( ) == aInts[i].type );          // stored at the width asked for
      bool bUnsigned = false;
      CHECK ( n.asInt ( bUnsigned ) == aInts[i].value );
      CHECK ( bUnsigned == ( aInts[i].type == MSGF_TYPE_UINT08
                          || aInts[i].type == MSGF_TYPE_UINT16
                          || aInts[i].type == MSGF_TYPE_UINT32
                          || aInts[i].type == MSGF_TYPE_UINT64 ) );
    }

    // A value the declared width could not hold is refused rather than
    // truncated. The kernel's own cast would have stored 44 for 300.
    CHECK_HR ( root.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, L"over", 300
                                        , MSGF_TYPE_INT08, 0, 0 ), MSGF_E_LIMIT );
    CHECK ( !root.exists ( L"over" ) );               // and wrote nothing

    // Reals, both widths. c_double() on a FLOAT raises in the kernel; asReal()
    // reads either.
    root.declareReal ( L"d", 2.5 );
    root.declareReal ( L"f", 1.5, Child, MSGF_TYPE_FLOAT );
    CHECK ( root.child ( L"d" ).asReal ( ) == 2.5 );
    CHECK ( root.child ( L"f" ).asReal ( ) == 1.5 );
    CHECK ( root.child ( L"f" ).type ( ) == MSGF_TYPE_FLOAT );

    // Wide text. WSTR16 -- 16-bit units, what the wire format and the Linux
    // port both assume -- is what declareText writes.
    root.declareText ( L"Greeting", L"Hello, \x20ACuro world" );
    Node greet = root.child ( L"Greeting" );
    CHECK ( greet.asText ( ) == L"Hello, \x20ACuro world" );
    CHECK ( greet.type ( ) == MSGF_TYPE_WSTR16 );

    // A blob is opaque bytes with a size -- no interpretation, no terminator,
    // and an embedded NUL is just a byte.
    const unsigned char aBytes[] = { 0x00, 0x2A, 0xFE, 0xFF, 0x00 };
    root.declareBlob ( L"Raw", aBytes, sizeof(aBytes) );
    std::vector<unsigned char> vBack = root.child ( L"Raw" ).asBlob ( );
    CHECK ( vBack.size ( ) == sizeof(aBytes) );
    CHECK ( vBack.size ( ) == sizeof(aBytes)
         && ::memcmp ( &vBack[0], aBytes, sizeof(aBytes) ) == 0 );

    // A GUID is a type of its own, rendered as canonical text.
    root.declareGuid ( L"Id", L"3F2504E0-4F89-11D3-9A0C-0305E82C3301" );
    CHECK ( root.child ( L"Id" ).asGuid ( ) == L"3F2504E0-4F89-11D3-9A0C-0305E82C3301" );
    CHECK ( root.child ( L"Id" ).type ( ) == MSGF_TYPE_GUID );

    // ToStringType()'s replacement: ONE vocabulary, both directions, so a
    // caller that writes a type out and reads it back never disagrees with
    // itself. (The FileSystem layer's ".type" file is exactly this string.)
    CHECK ( lib.typeName ( MSGF_TYPE_INT32 )  == L"INT32" );
    CHECK ( lib.typeName ( MSGF_TYPE_WSTR16 ) == L"WSTR16" );
    CHECK ( lib.typeFromName ( L"DOUBLE" ) == MSGF_TYPE_DOUBLE );

    wprintf ( L"  i32=%lld  d=%g  Greeting=\"%s\"  Id type=%s\n"
            , root.child ( L"i32" ).asInt ( )
            , root.child ( L"d" ).asReal ( )
            , greet.asText ( ).c_str ( )
            , lib.typeName ( root.child ( L"Id" ).type ( ) ).c_str ( ) );

    // Reading at the wrong family is an ANSWER. This is the check the original
    // could not write: there, it was a throw from inside the heap.
    CHECK_HR ( greet.get ( )->GetInt ( 0, 0 ), E_POINTER );
    long long iJunk = 0;
    CHECK_HR ( greet.get ( )->GetInt ( &iJunk, 0 ), MSGF_E_TYPE );
    double dJunk = 0;
    CHECK_HR ( root.child ( L"i32" ).get ( )->GetReal ( &dJunk ), MSGF_E_TYPE );
}


// =========================================================================
// 2. A named, typed node
// =========================================================================
//
// The original's P3PmsgField was "a P3PmsgName and a P3PmsgData glued
// together by inheritance", which is why oField.c_int() and oField == L"Name"
// both worked on it. The facade splits the two questions instead: name() and
// path() answer the naming half, asInt()/asText() the value half.
//
// The rule the original spent a paragraph on -- "a name-only field has NO data
// cell yet; assigning a P3PmsgData establishes one" -- disappears here. Every
// Declare* writes a name AND a value in one call, so the half-built state is
// not reachable.
//
static void Demo_NameAndValue ( Store& st )
{
    light::Section ( L"2. a named, typed node" );

    Node root = st.root ( );
    Node box  = root.declareText ( L"Box", L"" );

    box.declareInt ( L"Johnno", 42 );
    Node n = box.child ( L"Johnno" );
    CHECK ( n.name ( ) == L"Johnno" );
    CHECK ( n.path ( ) == L".Box.Johnno" );
    CHECK ( n.asInt ( ) == 42 );

    // Renaming in place: the value survives untouched. In the original this
    // was `oField = P3PmsgName(L"Larry")` -- an assignment whose meaning
    // depended on the TYPE of its right-hand side, since assigning a
    // P3PmsgData instead would have replaced the cell. Here they are two
    // differently-named calls.
    CHECK_HR ( box.get ( )->Rename ( MSGF_SCOPE_CHILD, L"Johnno", L"Larry" ), S_OK );
    CHECK ( !box.exists ( L"Johnno" ) );
    CHECK ( box.child ( L"Larry" ).asInt ( ) == 42 );

    // Renaming to the name it already has is S_FALSE -- a SUCCESS code. In
    // this ABI, "nothing to do" is never an error.
    CHECK_HR ( box.get ( )->Rename ( MSGF_SCOPE_CHILD, L"Larry", L"Larry" ), S_FALSE );
    CHECK_HR ( box.get ( )->Rename ( MSGF_SCOPE_CHILD, L"Ghost", L"X" ), MSGF_E_NO_ITEM );

    wprintf ( L"  renamed to '%s', value still %lld\n"
            , box.child ( L"Larry" ).name ( ).c_str ( )
            , box.child ( L"Larry" ).asInt ( ) );

    // THE NODE HANDLE IS A PATH, NOT A POINTER, and this is where that stops
    // being an implementation note. `n` was taken BEFORE the rename, so the
    // name it routes through is gone -- and it says so, rather than resolving
    // to whatever now occupies that block, which is what a raw kernel handle
    // would do.
    long long iVal = 0;
    CHECK_HR ( n.get ( )->GetInt ( &iVal, 0 ), MSGF_E_NO_ITEM );

    // Retype is the one way to change a node's type: a Set* keeps the declared
    // width, deliberately.
    CHECK_HR ( box.get ( )->Retype ( MSGF_SCOPE_CHILD, L"Larry", MSGF_TYPE_DOUBLE ), S_OK );
    CHECK ( box.child ( L"Larry" ).type ( ) == MSGF_TYPE_DOUBLE );
    CHECK ( box.child ( L"Larry" ).asReal ( ) == 0.0 );      // seeded with a zero
}


// =========================================================================
// 3. Descendants -- the child tree
// =========================================================================
//
// r_Desc() in the original; MSGF_SCOPE_CHILD here, which is the default
// argument, so it does not appear at a call site at all. Building a tree is
// still just a chain of declares, and every interior node is an ordinary node:
// there is no separate "node" type to learn, in either spelling.
//
static void Demo_Descendants ( Store& st )
{
    light::Section ( L"3. descendants -- building a tree" );

    Node root  = st.root ( );
    Node order = root.declareText ( L"Order", L"" );

    order.declareInt  ( L"OrderId",  10045 );
    order.declareText ( L"Customer", L"Ivyware Pty Ltd" );
    order.declareReal ( L"Total",    1299.50 );

    CHECK ( order.exists ( L"OrderId" ) );
    CHECK ( order.exists ( L"Customer" ) );
    CHECK ( !order.exists ( L"Missing" ) );          // absence is an ANSWER
    CHECK ( order.child ( L"OrderId" ).asInt ( ) == 10045 );
    CHECK ( order.child ( L"Customer" ).asText ( ) == L"Ivyware Pty Ltd" );
    CHECK ( order.child ( L"Total" ).asReal ( ) == 1299.50 );

    // The original wrote through a REFERENCE into the tree
    // (`oRoot.SelectItem(L"Total").c_double(1350.00`). Here the write is a
    // call on a path, which is why it is still correct after the tree has
    // moved underneath it.
    order.child ( L"Total" ).set ( 1350.00 );
    CHECK ( order.child ( L"Total" ).asReal ( ) == 1350.00 );

    // Nesting: declare into the child you just took. Two levels down.
    Node addr = order.declareText ( L"ShipTo", L"" );
    addr.declareText ( L"City",     L"Melbourne" );
    addr.declareInt  ( L"Postcode", 3000 );
    CHECK ( order.child ( L"ShipTo" ).exists ( L"City" ) );
    CHECK ( order.child ( L"ShipTo" ).child ( L"City" ).asText ( ) == L"Melbourne" );
    CHECK ( order.child ( L"ShipTo" ).child ( L"Postcode" ).asInt ( ) == 3000 );

    // ...or name the whole route in one string. This is the facade's own path
    // spelling: '.' before a descendant step, '@' before an attribute step.
    CHECK ( st.at ( L".Order.ShipTo.City" ).asText ( ) == L"Melbourne" );

    CHECK ( order.count ( ) == 4 );
    order.remove ( L"Total" );
    CHECK ( !order.exists ( L"Total" ) );
    CHECK ( order.count ( ) == 3 );

    wprintf ( L"  Order has %u children, ShipTo.City='%s'\n"
            , order.count ( )
            , st.at ( L".Order.ShipTo.City" ).asText ( ).c_str ( ) );

    // A declare onto an existing name is MSGF_E_EXISTS unless the caller says
    // it meant an update. The kernel's own default silently returns the
    // existing node, which makes "create" and "assign" indistinguishable at
    // the call site -- the original's `bUpdate` argument was the same knob
    // with the opposite default.
    CHECK_HR ( order.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, L"OrderId", 20090, 0, 0, 0 )
             , MSGF_E_EXISTS );
    order.declareInt ( L"OrderId", 20090 );          // the sugar asks for UPDATE
    CHECK ( order.child ( L"OrderId" ).asInt ( ) == 20090 );
    CHECK ( order.count ( ) == 3 );                  // no duplicate name

    // Truncate empties one collection, keeping the node.
    Node scratch = root.declareText ( L"Scratch", L"" );
    scratch.declareInt ( L"a", 1 );
    scratch.declareInt ( L"b", 2 );
    CHECK ( scratch.count ( ) == 2 );
    scratch.truncate ( );
    CHECK ( scratch.count ( ) == 0 );
}


// =========================================================================
// 4. Attributes -- the parallel tree
// =========================================================================
//
// Every node carries TWO child collections: its descendants and its
// attributes. The kernel exposes them through two parallel families of
// near-identical calls; here the collection is an ARGUMENT, which is the
// second of the facade's three reasons to exist.
//
// The original's other wrinkle disappears with it: descendants were created on
// demand but attributes were NOT -- the first write needed an explicit
// `r_Attr(P3PmsgField::AttrCMD_Create)`. Every Declare* here creates the
// collection it needs, in either scope.
//
static void Demo_Attributes ( Store& st )
{
    light::Section ( L"4. attributes -- metadata beside the value" );

    Node root = st.root ( );
    Node temp = root.declareReal ( L"Temperature", 21.5 );

    CHECK ( temp.count ( Attr ) == 0 );              // nothing there yet

    temp.declareText ( L"Unit",   L"Celsius", Attr );
    temp.declareInt  ( L"Sensor", 7,          Attr );

    CHECK ( temp.exists ( L"Unit",   Attr ) );
    CHECK ( temp.exists ( L"Sensor", Attr ) );
    CHECK ( !temp.exists ( L"Nobody", Attr ) );
    CHECK ( temp.count ( Attr ) == 2 );

    CHECK ( temp.asReal ( ) == 21.5 );               // the value is untouched

    // The two collections are independent, and a name may appear in both.
    temp.declareReal ( L"Reading", 21.5 );
    temp.declareText ( L"Reading", L"in the attribute scope", Attr );
    CHECK ( temp.count ( )      == 1 );
    CHECK ( temp.count ( Attr ) == 3 );
    CHECK ( temp.child ( L"Reading" ).asReal ( ) == 21.5 );
    CHECK ( temp.child ( L"Reading", Attr ).asText ( ) == L"in the attribute scope" );

    // ...and a path says which is which.
    CHECK ( temp.child ( L"Unit", Attr ).path ( ) == L".Temperature@Unit" );
    CHECK ( st.at ( L".Temperature@Unit" ).asText ( ) == L"Celsius" );

    wprintf ( L"  Temperature=%.1f  @Unit='%s'  @Sensor=%lld\n"
            , temp.asReal ( )
            , temp.child ( L"Unit",   Attr ).asText ( ).c_str ( )
            , temp.child ( L"Sensor", Attr ).asInt ( ) );

    // Truncate is SCOPED here. The kernel's own P3PmsgField::Truncate drops
    // the descendants AND the attributes AND the position stack -- one call,
    // three effects.
    temp.truncate ( Attr );
    CHECK ( temp.count ( Attr ) == 0 );
    CHECK ( temp.count ( )      == 1 );              // the descendants survive
}


// =========================================================================
// 5. A scoped override -- by hand, and by MsgStck
// =========================================================================
//
// The original's fifth section demonstrated P3PmsgField::r_Stck() -- a per-field
// value stack: Push() saved the field's current name and data INSIDE the field,
// Pop() restored it. A scoped override for a speculative edit.
//
// THIS SECTION USED TO SAY THE FACADE DID NOT EXPOSE IT, and argued that it did
// not need to: the stack's whole purpose is to let a caller hold a value across
// a mutation without holding a handle to it, and holding a handle across a
// mutation is precisely what a path-based node makes safe. That argument still
// holds for the ORDINARY case, and the first half below is it -- three lines, no
// kernel state, nothing to leak if the caller never restores.
//
// ABI 2 EXPOSES IT ANYWAY, for the two things the hand-rolled version cannot do,
// and the second half is those:
//
//   * THE NAME TRAVELS WITH THE VALUE. A `std::wstring saved` holds a value; the
//     kernel's slot holds the pair.
//   * THE SAVED PAIR LIVES IN THE STORE. It survives everything the node
//     survives, and ANY reference to that node can pop what another one pushed
//     -- which a local variable in one function cannot offer to another.
//
// And it NESTS, which is worth stating because every tier of this repository
// documented the opposite until a client pushed twice: the kernel merely
// ASSERTED that its saved slot was empty while implementing a linked stack, so
// a second push tripped an assertion in a debug build and the documentation was
// written from the assertion rather than from the code. The assertion is gone.
//
static void Demo_ScopedOverride ( Store& st )
{
    light::Section ( L"5. a scoped override -- by hand, and by MsgStck" );

    Node root  = st.root ( );
    Node field = root.declareText ( L"Larry", L"Data" );

    // --- by hand ----------------------------------------------------------
    const std::wstring strSaved = field.asText ( );

    field.set ( std::wstring ( L"Larry-Pushed" ) );
    CHECK ( field.asText ( ) == L"Larry-Pushed" );

    field.set ( strSaved );
    CHECK ( field.asText ( ) == L"Data" );           // the override unwound

    // And the point of doing it this way: `field` is a route, so the restore
    // is still correct after the store has grown underneath it 200 times --
    // which is the case the kernel's stack existed to survive.
    for ( int i = 0; i < 200; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"pad%03d", i );
      root.declareText ( szName, L"padding padding padding padding" );
    }
    field.set ( std::wstring ( L"speculative" ) );
    CHECK ( field.asText ( ) == L"speculative" );
    field.set ( strSaved );
    CHECK ( field.asText ( ) == L"Data" );

    wprintf ( L"  value after restore: '%s' (across 200 heap growths)\n"
            , field.asText ( ).c_str ( ) );

    // --- and by the node's own slot (ABI 2) --------------------------------
    CHECK ( !field.stacked ( ) );
    field.pushValue ( );
    CHECK ( field.stacked ( ) );

    field.set ( std::wstring ( L"speculative again" ) );
    CHECK ( field.asText ( ) == L"speculative again" );

    CHECK ( field.popValue ( ) );                    // TRUE: it restored one
    CHECK ( field.asText ( ) == L"Data" );
    CHECK ( !field.stacked ( ) );

    // popValue answers whether it DID anything, which is what makes a drain
    // loop terminate -- the kernel's own Pop is a silent no-op on an empty
    // slot and reports nothing.
    CHECK ( !field.popValue ( ) );

    // drop forgets the pair instead of restoring it.
    field.pushValue ( );
    field.set ( std::wstring ( L"kept" ) );
    CHECK ( field.dropValue ( ) );
    CHECK ( field.asText ( ) == L"kept" );           // NOT rolled back
    CHECK ( !field.stacked ( ) );
    field.set ( strSaved );

    // It NESTS: push, push, pop, pop unwinds in order.
    field.set ( std::wstring ( L"one" ) );
    field.pushValue ( );  field.set ( std::wstring ( L"two" ) );
    field.pushValue ( );  field.set ( std::wstring ( L"three" ) );
    CHECK ( field.popValue ( ) );
    CHECK ( field.asText ( ) == L"two" );
    CHECK ( field.stacked ( ) );                     // one still below
    CHECK ( field.popValue ( ) );
    CHECK ( field.asText ( ) == L"one" );
    CHECK ( !field.stacked ( ) );

    // The pair lives in the STORE, so a second route to the node sees it.
    Node other = root.child ( L"Larry" );
    field.pushValue ( );
    CHECK ( other.stacked ( ) );
    CHECK ( other.popValue ( ) );
    CHECK ( !field.stacked ( ) );

    wprintf ( L"  MsgStck reached through ABI 2: nests, and the slot is in the store\n" );
}


// =========================================================================
// 6. How failure is reported
// =========================================================================
//
// The original's sixth section was about P2Pevent: Msgcore does not return
// error codes and does not throw std::exception -- it throws a POINTER to a
// fluently-built event object which the catcher must then dispose of
// (Cancel(true) to report it, Cancel(false) to discard, Isolate() to carry it
// elsewhere). Forget the Cancel and it leaks; catch it in the wrong place and
// it displays a message box.
//
// Every facade entry point catches that and answers an HRESULT. So this
// section is the same subject with the mechanism replaced: a NUMBER with a
// stable meaning, which a caller can compare rather than parse.
//
// The load-bearing limit the original used to provoke a throw is still here,
// only bigger: a name is at most MAX_NAME units, and an over-long one is
// refused BEFORE anything is written, so the live node is left intact.
//
static void Demo_Errors ( Store& st )
{
    light::Section ( L"6. how failure is reported" );

    Node    root = st.root ( );
    Library lib;

    root.declareText ( L"keep", L"intact" );

    // Names the model refuses, each with the SAME code, so a caller has one
    // thing to test for. '.' and '@' are refused because they are the path
    // grammar's two separators -- which is what makes GetPath reversible.
    CHECK ( !lib.validName ( L"" ) );
    CHECK ( !lib.validName ( L"a.b" ) );
    CHECK ( !lib.validName ( L"a@b" ) );
    CHECK ( !lib.validName ( L"a:b" ) );
    CHECK (  lib.validName ( L"Title" ) );

    CHECK_HR ( root.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, L"",    1, 0, 0, 0 ), MSGF_E_NAME );
    CHECK_HR ( root.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, L"a.b", 1, 0, 0, 0 ), MSGF_E_NAME );
    CHECK_HR ( root.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, L"a@b", 1, 0, 0, 0 ), MSGF_E_NAME );

    // The length bound, and the fact that it counts UTF-16 UNITS rather than
    // code points -- one astral code point costs two, exactly as it did for
    // the original's 63-unit P3PmsgName. It IS that same bound: writing this
    // section is what measured MsgFacade's MAX_NAME as wrong (it said 127,
    // which is what the core's VALIDATOR answers; the core's STORAGE is 63,
    // and a name in between aborted the process instead of returning a code).
    // The facade now enforces 63 -- see the note on MAX_NAME in MsgFacade.h.
    const std::wstring strOk ( MAX_NAME, L'a' );
    CHECK_HR ( root.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, strOk.c_str ( ), 1, 0, 0, 0 ), S_OK );

    const std::wstring strTooLong ( MAX_NAME + 1, L'a' );
    CHECK_HR ( root.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, strTooLong.c_str ( ), 1, 0, 0, 0 )
             , MSGF_E_NAME );
    CHECK ( !root.exists ( strTooLong ) );           // the refusal wrote nothing
    CHECK ( root.child ( L"keep" ).asText ( ) == L"intact" );   // and touched nothing

    std::wstring strAstral;
    for ( unsigned int i = 0; i < MAX_NAME / 2; ++i ) strAstral += L"\U0001F680";
    CHECK_HR ( root.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, strAstral.c_str ( ), 1, 0, 0, 0 )
             , S_OK );                               // 31 rockets = 62 units
    strAstral += L"\U0001F680";                      // 32 rockets = 64 units
    CHECK_HR ( root.get ( )->DeclareInt ( MSGF_SCOPE_CHILD, strAstral.c_str ( ), 1, 0, 0, 0 )
             , MSGF_E_NAME );

    // The other codes a caller meets in ordinary use, each provoked once.
    CHECK_HR ( root.get ( )->Exists ( 99, L"keep" ), MSGF_E_SCOPE );
    CHECK_HR ( root.get ( )->GetChild ( MSGF_SCOPE_CHILD, L"Ghost", 0 ), E_POINTER );
    CHECK_HR ( st.get ( )->NodeFromPath ( L"keep", 0 ), E_POINTER );

    IMsgNode *pNode = 0;
    CHECK_HR ( st.get ( )->NodeFromPath ( L"keep", &pNode ), MSGF_E_PATH );  // no leading '.'
    CHECK_HR ( st.get ( )->NodeFromPath ( L".Ghost", &pNode ), MSGF_E_NO_ITEM );
    CHECK_HR ( st.get ( )->NodeFromPos ( 0xDEADBEEF, &pNode ), MSGF_E_NO_POS );

    // And through the sugar layer, the same code arrives as an exception
    // CARRYING it -- so a caller that prefers exceptions still gets to
    // dispatch on the reason.
    bool bThrew = false;
    HRESULT hrCaught = S_OK;
    try                        { root.child ( L"Ghost" ); }
    catch ( const Error& e )   { bThrew = true; hrCaught = e.code ( ); }
    CHECK ( bThrew );
    CHECK ( hrCaught == MSGF_E_NO_ITEM );

    wprintf ( L"  a missing child answers %s, in both spellings\n"
            , light::HrName ( hrCaught ) );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    light::InitConsole ( );
    wprintf ( L"=== DataFieldTest (Light) - the Msgcore data model through MsgFacade ===\n" );

    try
    {
      Library lib;
      wprintf ( L"%s\n", lib.version ( ).c_str ( ) );

      // Each section gets its own store, so a failure in one cannot make the
      // next one lie. Creating six stores costs six small heaps.
      { Store st = lib.createStore ( ); Demo_TypedCells     ( st ); }
      { Store st = lib.createStore ( ); Demo_NameAndValue   ( st ); }
      { Store st = lib.createStore ( ); Demo_Descendants    ( st ); }
      { Store st = lib.createStore ( ); Demo_Attributes     ( st ); }
      { Store st = lib.createStore ( ); Demo_ScopedOverride ( st ); }
      { Store st = lib.createStore ( ); Demo_Errors         ( st ); }
    }
    catch ( const Error& e )
    {
      ++light::Failed ( );
      wprintf ( L"\nUNEXPECTED msgf::Error: %s (0x%08X)\n"
              , light::HrName ( e.code ( ) ), (unsigned)e.code ( ) );
    }
    catch ( ... )
    {
      ++light::Failed ( );
      wprintf ( L"\nUNEXPECTED non-msgf exception\n" );
    }

    return light::Verdict ( );
}
