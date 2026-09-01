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
// MgrCApiTest.cpp  (Light -- MsgFacade)
//
// THE SAME STORE, THROUGH THE FLAT ABI. The original
// (..\DirectExamples\MgrCApiTest) is Msgcore through Msgcore_c.h:
// opaque handles, plain extern "C" functions, no exceptions, no destructors.
// This one is the same subject through the OTHER flat surface over the same
// kernel -- MsgFacade's vtable ABI, used RAW: this file includes MsgFacade.h
// and NOT MsgFacadeFn.hpp, so there is no RAII, no std::wstring, and every
// call site shows the ABI exactly as a non-C++ binding would have to use it.
//
// That is the point of keeping this harness: it is the one that answers "what
// does a language binding see", and the answer changed in three ways.
//
// ---------------------------------------------------------------------------
// THE ORIGINAL'S TWO RULES, AND WHAT BECAME OF THEM
// ---------------------------------------------------------------------------
//
//  1. "EVERY HANDLE YOU ARE GIVEN, YOU MUST DESTROY." -- STILL TRUE, and it is
//     the one rule this ABI keeps. msgcore_field_destroy became Release, and a
//     Declare* that hands back a node still hands back something to release.
//     The one softening: `outNode` may be NULL when the caller does not want
//     the child back, so the original's
//         msgcore_field_destroy(msgcore_field_declare_wstr(...))
//     -- destroy-what-you-only-wanted-the-side-effect-of -- is now just a 0.
//
//  2. "LIVE vs DETACHED." -- GONE, and this is the substantive difference.
//     In the C API, msgcore_mgr_root / msgcore_field_child gave a LIVE alias
//     of the heap node (writes land) while msgcore_mgr_as_field /
//     msgcore_field_select_item gave a DETACHED DEEP COPY (writes are
//     silently discarded -- no error, no failure, just gone). A binding had to
//     know which of two identically-typed handles it was holding.
//
//     Every IMsgNode here is a ROUTE to a place in the store, so there is no
//     second kind. Section 1 shows the two calls that used to differ agreeing,
//     and shows a write through a node fetched BY POSITION landing -- which in
//     the original was exactly the detached case.
//
//  3. AND THE SHARP EDGE THAT IS GONE. The original's header spends a page on
//     this and it is worth restating, because it is the strongest argument
//     for a facade over a thin C shim: Msgcore_c.h's typed scalar accessors
//     are NOT exception-safe. msgcore_field_get_int is literally
//
//         if (!hField) return 0;
//         return toField(hField)->c_int();
//
//     so a type mismatch throws a P2Pevent* straight out of the DLL. And it
//     cannot reliably be caught: the header is extern "C", and under MSVC's
//     /EHsc model an extern "C" function is ASSUMED NOT TO THROW, so the
//     optimiser may delete the handler you wrapped the call in -- which it
//     did, in that very harness, in Release and not in Debug.
//
//     Here the same mistake is MSGF_E_TYPE. Section 2 makes the call the
//     original had to describe rather than perform.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 1 = SETUP, 3 = at least one check failed.

#include "MsgFacade.h"
#include "LightHarness.h"

using namespace msgf;

// The whole vocabulary this file needs, since there is no sugar layer.
static IMsgStore*
NewStore ( IMsgLibrary *pLib )
{
    IMsgStore *pStore = 0;
    pLib->CreateStore ( MSGF_ADDR_64, 4096, 1u << 20, &pStore );
    return pStore;
}


// =========================================================================
// 1. There is only one kind of handle
// =========================================================================
//
// The original's first section proved that a write through a detached handle
// went nowhere. There is nothing to prove here, so this section proves the
// negative instead: the three ways of naming a node all give the same node,
// and all three are writable.
//
static void Demo_OneKindOfHandle ( IMsgLibrary *pLib )
{
    light::Section ( L"1. one kind of handle -- every node is live" );

    IMsgStore *pStore = NewStore ( pLib );
    IMsgNode  *pRoot  = 0;
    CHECK_HR ( pStore->GetRoot ( &pRoot ), S_OK );

    // A declare that does not want the node back passes 0 for it -- the
    // original had to accept a handle and immediately destroy it.
    CHECK_HR ( pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Real", L"kept", 0, 0 ), S_OK );
    CHECK_HR ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"Real" ), S_OK );

    // (a) by name from the parent
    IMsgNode *pByName = 0;
    CHECK_HR ( pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Real", &pByName ), S_OK );

    // (b) by path from the store
    IMsgNode *pByPath = 0;
    CHECK_HR ( pStore->NodeFromPath ( L".Real", &pByPath ), S_OK );

    // (c) by position -- THE detached case in the C API
    unsigned long long uPos = 0;
    CHECK_HR ( pByName->GetPos ( &uPos ), S_OK );
    IMsgNode *pByPos = 0;
    CHECK_HR ( pStore->NodeFromPos ( uPos, &pByPos ), S_OK );

    // A write through each of the three, read back through the other two.
    CHECK_HR ( pByPath->SetText ( L"written by path" ), S_OK );
    wchar_t szText[64]; unsigned int cch = 64;
    CHECK_HR ( pByName->GetText ( szText, &cch ), S_OK );
    CHECK ( ::wcscmp ( szText, L"written by path" ) == 0 );

    CHECK_HR ( pByPos->SetText ( L"written by position" ), S_OK );
    cch = 64;
    CHECK_HR ( pByName->GetText ( szText, &cch ), S_OK );
    CHECK ( ::wcscmp ( szText, L"written by position" ) == 0 );

    wprintf ( L"  name, path and position all name one live node\n" );

    // Rule 1 survives intact: everything handed out is released, in any order.
    pByPos->Release ( );
    pByPath->Release ( );
    pByName->Release ( );

    // ...and releasing the STORE while nodes are outstanding is legal. It
    // CLOSES it -- every outstanding object starts answering MSGF_E_CLOSED --
    // and the last object out frees it. A half-alive store that still served
    // reads would be a much worse contract than one that says so.
    IMsgNode *pKeep = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Real", &pKeep );
    pStore->Release ( );

    cch = 64;
    CHECK_HR ( pKeep->GetText ( szText, &cch ), MSGF_E_CLOSED );
    CHECK_HR ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"n", 1, 0, 0, 0 ), MSGF_E_CLOSED );
    pKeep->Release ( );
    pRoot->Release ( );
}


// =========================================================================
// 2. Typed declares and reads
// =========================================================================
//
// One Declare* per family, with the storage width as an argument rather than
// as a suffix in the function name -- so a binding generator emits five
// methods here where the C API needed one per type.
//
// And the part the original could only describe: reading a cell at the wrong
// type. There it was an uncatchable throw, and the harness had to dispatch on
// msgcore_field_get_data_type to avoid ever making the call. Here the call is
// safe to make, so it is made.
//
static void Demo_TypedValues ( IMsgLibrary *pLib )
{
    light::Section ( L"2. typed declares and reads" );

    IMsgStore *pStore = NewStore ( pLib );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    CHECK_HR ( pRoot->DeclareInt  ( MSGF_SCOPE_CHILD, L"Count",   42,        0, 0, 0 ), S_OK );
    CHECK_HR ( pRoot->DeclareInt  ( MSGF_SCOPE_CHILD, L"Bytes",   1LL << 40, MSGF_TYPE_INT64, 0, 0 ), S_OK );
    CHECK_HR ( pRoot->DeclareReal ( MSGF_SCOPE_CHILD, L"Ratio",   0.625,     0, 0, 0 ), S_OK );
    CHECK_HR ( pRoot->DeclareInt  ( MSGF_SCOPE_CHILD, L"Enabled", 1,         MSGF_TYPE_BOOL, 0, 0 ), S_OK );
    CHECK_HR ( pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Label",   L"widget", 0, 0 ), S_OK );

    struct { int nId; double d; } oBlob = { 9, 1.25 };
    CHECK_HR ( pRoot->DeclareBlob ( MSGF_SCOPE_CHILD, L"Raw", &oBlob, (unsigned)sizeof(oBlob), 0, 0 )
             , S_OK );

    // The type vocabulary, exactly as the original used msgcore_type_name /
    // msgcore_type_from_name -- and, as there, it round-trips.
    struct { const wchar_t *lpszName; const wchar_t *lpszType; } aExpect[] = {
      { L"Count",   L"INT32"  },
      { L"Bytes",   L"INT64"  },
      { L"Ratio",   L"DOUBLE" },
      { L"Enabled", L"BOOL"   },
      { L"Label",   L"WSTR16" },
      { L"Raw",     L"BLOB16" },
    };

    for ( int i = 0; i < (int)( sizeof(aExpect) / sizeof(aExpect[0]) ); ++i )
    {
      IMsgNode *pNode = 0;
      CHECK_HR ( pRoot->GetChild ( MSGF_SCOPE_CHILD, aExpect[i].lpszName, &pNode ), S_OK );
      if ( !pNode ) continue;

      unsigned char uType = 0;
      CHECK_HR ( pNode->GetType ( &uType ), S_OK );

      wchar_t szType[32]; unsigned int cch = 32;
      CHECK_HR ( pLib->TypeName ( uType, szType, &cch ), S_OK );
      CHECK ( ::wcscmp ( szType, aExpect[i].lpszType ) == 0 );

      unsigned char uBack = 0;
      CHECK_HR ( pLib->TypeFromName ( szType, &uBack ), S_OK );
      CHECK ( uBack == uType );

      wprintf ( L"  %-8s type=%s\n", aExpect[i].lpszName, szType );
      pNode->Release ( );
    }

    // Read, write, read back -- through a node that outlives the write, which
    // in the C API meant re-fetching the child each time.
    IMsgNode *pCount = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Count", &pCount );
    long long iVal = 0;
    CHECK_HR ( pCount->GetInt ( &iVal, 0 ), S_OK );
    CHECK ( iVal == 42 );
    CHECK_HR ( pCount->SetInt ( 43 ), S_OK );
    CHECK_HR ( pCount->GetInt ( &iVal, 0 ), S_OK );
    CHECK ( iVal == 43 );

    // THE CALL THE ORIGINAL COULD NOT MAKE.
    //
    // msgcore_field_get_int on an INT64 cell threw "Incompatible c_int() type
    // (int64)" out of the DLL, past a catch the optimiser had deleted. Here
    // the width-agnostic read simply WORKS on any integer cell, and asking a
    // text node for an integer is a code.
    IMsgNode *pBytes = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Bytes", &pBytes );
    int bUnsigned = 1;
    CHECK_HR ( pBytes->GetInt ( &iVal, &bUnsigned ), S_OK );
    CHECK ( iVal == ( 1LL << 40 ) );
    CHECK ( bUnsigned == 0 );

    IMsgNode *pLabel = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Label", &pLabel );
    CHECK_HR ( pLabel->GetInt ( &iVal, 0 ), MSGF_E_TYPE );      // an ANSWER
    double dVal = 0;
    CHECK_HR ( pLabel->GetReal ( &dVal ), MSGF_E_TYPE );
    CHECK_HR ( pBytes->SetText ( L"nope" ), MSGF_E_TYPE );

    // THE CALLER-SIZED BUFFER PROTOCOL, which the C API did not have: its
    // msgcore_field_get_wstr handed back an interior pointer into the heap,
    // valid until the next allocation. Here the caller owns the memory.
    unsigned int cch = 0;
    CHECK_HR ( pLabel->GetText ( 0, &cch ), S_OK );             // a size query
    CHECK ( cch == 7 );                                         // "widget" + NUL

    wchar_t szSmall[4]; cch = 4;
    CHECK_HR ( pLabel->GetText ( szSmall, &cch )
             , HRESULT_FROM_WIN32 ( ERROR_MORE_DATA ) );
    CHECK ( cch == 7 );                                         // still says what it needs

    wchar_t szExact[7]; cch = 7;
    CHECK_HR ( pLabel->GetText ( szExact, &cch ), S_OK );
    CHECK ( ::wcscmp ( szExact, L"widget" ) == 0 );

    // Bytes come back the same way, and a zero-length blob is a real value --
    // which is how "present and empty" is told from "absent".
    IMsgNode *pRaw = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Raw", &pRaw );
    unsigned int cb = 0;
    CHECK_HR ( pRaw->GetBlob ( 0, &cb ), S_OK );
    CHECK ( cb == sizeof(oBlob) );
    unsigned char aBack[32]; cb = sizeof(aBack);
    CHECK_HR ( pRaw->GetBlob ( aBack, &cb ), S_OK );
    CHECK ( ::memcmp ( aBack, &oBlob, sizeof(oBlob) ) == 0 );

    pRaw->Release ( ); pLabel->Release ( ); pBytes->Release ( ); pCount->Release ( );
    pRoot->Release ( ); pStore->Release ( );
}


// =========================================================================
// 3. Navigation and enumeration
// =========================================================================
//
// GetChild descends one level at a time, each step producing a handle the
// caller owns -- the same shape as msgcore_field_child. A cursor walks a
// collection whose shape the caller does not know, which is what
// msgcore_curs_* was for.
//
static void Demo_Navigate ( IMsgLibrary *pLib )
{
    light::Section ( L"3. navigation and enumeration" );

    IMsgStore *pStore = NewStore ( pLib );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    // Build .Config.Network.{Host,Port} -- and keep each level's node, which
    // in the C API meant a matching destroy on every exit path.
    IMsgNode *pCfg = 0, *pNet = 0;
    CHECK_HR ( pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Config",  L"", 0, &pCfg ), S_OK );
    CHECK_HR ( pCfg->DeclareText  ( MSGF_SCOPE_CHILD, L"Network", L"", 0, &pNet ), S_OK );
    CHECK_HR ( pNet->DeclareText  ( MSGF_SCOPE_CHILD, L"Host", L"127.0.0.1", 0, 0 ), S_OK );
    CHECK_HR ( pNet->DeclareInt   ( MSGF_SCOPE_CHILD, L"Port", 7801, 0, 0, 0 ), S_OK );

    // Descend and read.
    IMsgNode *pHost = 0, *pPort = 0;
    CHECK_HR ( pNet->GetChild ( MSGF_SCOPE_CHILD, L"Host", &pHost ), S_OK );
    CHECK_HR ( pNet->GetChild ( MSGF_SCOPE_CHILD, L"Port", &pPort ), S_OK );

    wchar_t szHost[32]; unsigned int cch = 32;
    CHECK_HR ( pHost->GetText ( szHost, &cch ), S_OK );
    CHECK ( ::wcscmp ( szHost, L"127.0.0.1" ) == 0 );
    long long iPort = 0;
    CHECK_HR ( pPort->GetInt ( &iPort, 0 ), S_OK );
    CHECK ( iPort == 7801 );

    // A missing child is a CODE, not a NULL handle to test for -- so a binding
    // that checks HRESULTs at all cannot miss it.
    IMsgNode *pNope = 0;
    CHECK_HR ( pNet->GetChild ( MSGF_SCOPE_CHILD, L"Nope", &pNope ), MSGF_E_NO_ITEM );
    CHECK ( pNope == 0 );

    // Enumerate the level with a cursor.
    IMsgCursor *pCurs = 0;
    CHECK_HR ( pNet->OpenCursor ( MSGF_SCOPE_CHILD, &pCurs ), S_OK );

    unsigned int uCount = 0;
    CHECK_HR ( pCurs->GetCount ( &uCount ), S_OK );
    CHECK ( uCount == 2 );

    int nSeen = 0;
    for ( int bEnd = 0; SUCCEEDED ( pCurs->IsEnd ( &bEnd ) ) && !bEnd; pCurs->Next ( ) )
    {
      wchar_t szName[MAX_NAME + 1]; unsigned int cchName = MAX_NAME + 1;
      CHECK_HR ( pCurs->GetName ( szName, &cchName ), S_OK );
      unsigned int uKind = 0, uIndex = 0;
      pCurs->GetKind  ( &uKind );
      pCurs->GetIndex ( &uIndex );
      wprintf ( L"  Config.Network[%u] = %s (kind=%u)\n", uIndex, szName, uKind );
      ++nSeen;
    }
    CHECK ( nSeen == 2 );

    // Advancing from the end is S_FALSE -- a SUCCESS code. The core's own
    // operator++ RAISES there, and its IsEoCursor answers TRUE while standing
    // ON the last element, so a loop driven by the core's predicate would have
    // visited one of these two and then thrown.
    CHECK_HR ( pCurs->Next ( ), S_FALSE );
    CHECK_HR ( pCurs->GetName ( 0, 0 ), MSGF_E_RANGE );
    CHECK_HR ( pCurs->Rewind ( ), S_OK );
    pCurs->Release ( );

    // ...or without a cursor at all, by index into the collection.
    for ( unsigned int i = 0; i < 2; ++i )
    {
      IMsgNode *pAt = 0;
      CHECK_HR ( pNet->GetChildAt ( MSGF_SCOPE_CHILD, i, &pAt ), S_OK );
      if ( pAt ) pAt->Release ( );
    }
    IMsgNode *pPast = 0;
    CHECK_HR ( pNet->GetChildAt ( MSGF_SCOPE_CHILD, 9, &pPast ), MSGF_E_RANGE );

    pPort->Release ( ); pHost->Release ( ); pNet->Release ( ); pCfg->Release ( );
    pRoot->Release ( ); pStore->Release ( );
}


// =========================================================================
// 4. Positions, paths and persistence
// =========================================================================
//
// The same positional handle the C++ side calls P2Pos and the C API called
// msgcore_field_get_p2pos: stable, integral, resolvable back to a node.
//
// msgcore_mgr_p2pos2path returned a THREAD-LOCAL buffer valid only until the
// next call on the same thread -- a trap every binding had to document. Here
// the path comes back through the caller-sized buffer protocol, so it is the
// caller's memory and lasts as long as the caller wants it to.
//
static void Demo_PosAndPersist ( IMsgLibrary *pLib )
{
    light::Section ( L"4. positions, paths, save / load" );

    const std::wstring strFile = light::TempPath ( L"mscs_light_capi" );
    ::DeleteFileW ( strFile.c_str ( ) );

    unsigned long long uPos = 0;

    // ---- writer ---------------------------------------------------------
    {
      IMsgStore *pStore = NewStore ( pLib );
      IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

      IMsgNode *pDev = 0;
      pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Device", L"", 0, &pDev );
      pDev->DeclareText ( MSGF_SCOPE_CHILD, L"Serial",   L"SN-000123", 0, 0 );
      pDev->DeclareInt  ( MSGF_SCOPE_CHILD, L"Revision", 4, 0, 0, 0 );

      IMsgNode *pSerial = 0;
      pDev->GetChild ( MSGF_SCOPE_CHILD, L"Serial", &pSerial );
      CHECK_HR ( pSerial->GetPos ( &uPos ), S_OK );
      CHECK ( uPos != 0 );

      wchar_t szPath[256]; unsigned int cch = 256;
      CHECK_HR ( pSerial->GetPath ( szPath, &cch ), S_OK );
      CHECK ( ::wcscmp ( szPath, L".Device.Serial" ) == 0 );
      wprintf ( L"  pos %llu -> '%s'\n", uPos, szPath );

      // Resolve the position back. In the C API this was a detached copy --
      // fine for reading, which is all an inode lookup needs, but nothing
      // more. Here it is a node, so an inode lookup can be the START of a
      // write rather than the end of a read.
      IMsgNode *pByPos = 0;
      CHECK_HR ( pStore->NodeFromPos ( uPos, &pByPos ), S_OK );
      wchar_t szName[MAX_NAME + 1]; cch = MAX_NAME + 1;
      CHECK_HR ( pByPos->GetName ( szName, &cch ), S_OK );
      CHECK ( ::wcscmp ( szName, L"Serial" ) == 0 );
      pByPos->Release ( );

      CHECK_HR ( pStore->Save ( strFile.c_str ( ), 0 ), S_OK );
      int bDirty = 1;
      CHECK_HR ( pStore->IsDirty ( &bDirty ), S_OK );
      CHECK ( bDirty == 0 );

      pSerial->Release ( ); pDev->Release ( ); pRoot->Release ( );
      pStore->Release ( );                 // closes the file
    }

    // ---- reader ---------------------------------------------------------
    {
      IMsgStore *pStore = 0;
      CHECK_HR ( pLib->OpenStore ( strFile.c_str ( ), &pStore ), S_OK );

      int bValid = 0;
      CHECK_HR ( pStore->IsValid ( &bValid ), S_OK );
      CHECK ( bValid != 0 );

      IMsgNode *pSer = 0;
      CHECK_HR ( pStore->NodeFromPath ( L".Device.Serial", &pSer ), S_OK );
      wchar_t szText[64]; unsigned int cch = 64;
      CHECK_HR ( pSer->GetText ( szText, &cch ), S_OK );
      CHECK ( ::wcscmp ( szText, L"SN-000123" ) == 0 );

      // The position survived the round trip to disk.
      unsigned long long uBack = 0;
      CHECK_HR ( pSer->GetPos ( &uBack ), S_OK );
      CHECK ( uBack == uPos );
      wprintf ( L"  reloaded, Serial still at pos %llu\n", uBack );

      pSer->Release ( ); pStore->Release ( );
    }

    ::DeleteFileW ( strFile.c_str ( ) );
}


// =========================================================================
// 5. Structural edits -- rename, move, retype
// =========================================================================
//
// These three exist because a filesystem needs them and none is expressible as
// "declare over the top": a rename must keep the value, a move must keep the
// node, and a retype must replace the cell in place. All three act on a CHILD
// BY NAME of a parent node, exactly as their C API counterparts did.
//
static void Demo_StructuralEdits ( IMsgLibrary *pLib )
{
    light::Section ( L"5. rename / move / retype" );

    IMsgStore *pStore = NewStore ( pLib );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgNode *pIn = 0, *pOut = 0;
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Inbox",  L"", 0, &pIn );
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Outbox", L"", 0, &pOut );
    pIn->DeclareText ( MSGF_SCOPE_CHILD, L"draft", L"hello", 0, 0 );

    // Rename in place: the value follows the name.
    CHECK_HR ( pIn->Rename ( MSGF_SCOPE_CHILD, L"draft", L"letter" ), S_OK );
    CHECK_HR ( pIn->Exists ( MSGF_SCOPE_CHILD, L"draft" ),  S_FALSE );
    CHECK_HR ( pIn->Exists ( MSGF_SCOPE_CHILD, L"letter" ), S_OK );
    {
      IMsgNode *pN = 0;
      pIn->GetChild ( MSGF_SCOPE_CHILD, L"letter", &pN );
      wchar_t szText[32]; unsigned int cch = 32;
      pN->GetText ( szText, &cch );
      CHECK ( ::wcscmp ( szText, L"hello" ) == 0 );
      pN->Release ( );
    }

    // Move between parents: one node, two collections.
    CHECK_HR ( pIn->Move ( MSGF_SCOPE_CHILD, L"letter", pOut ), S_OK );
    CHECK_HR ( pIn->Exists  ( MSGF_SCOPE_CHILD, L"letter" ), S_FALSE );
    CHECK_HR ( pOut->Exists ( MSGF_SCOPE_CHILD, L"letter" ), S_OK );
    {
      IMsgNode *pN = 0;
      pOut->GetChild ( MSGF_SCOPE_CHILD, L"letter", &pN );
      wchar_t szPath[128]; unsigned int cch = 128;
      pN->GetPath ( szPath, &cch );
      CHECK ( ::wcscmp ( szPath, L".Outbox.letter" ) == 0 );     // the path followed it
      pN->Release ( );
    }

    // Retype: replace the cell, changing the type tag with it, and seed a zero
    // value of the new type.
    CHECK_HR ( pOut->Retype ( MSGF_SCOPE_CHILD, L"letter", MSGF_TYPE_INT32 ), S_OK );
    {
      IMsgNode *pN = 0;
      pOut->GetChild ( MSGF_SCOPE_CHILD, L"letter", &pN );
      unsigned char uType = 0;
      pN->GetType ( &uType );
      CHECK ( uType == MSGF_TYPE_INT32 );
      long long iVal = -1;
      CHECK_HR ( pN->GetInt ( &iVal, 0 ), S_OK );
      CHECK ( iVal == 0 );
      CHECK_HR ( pN->SetInt ( 99 ), S_OK );
      pN->Release ( );
    }
    wprintf ( L"  draft -> letter, Inbox -> Outbox, WSTR16 -> INT32\n" );

    // Each one names what went wrong rather than answering 0 for everything.
    CHECK_HR ( pOut->Rename ( MSGF_SCOPE_CHILD, L"ghost", L"x" ),    MSGF_E_NO_ITEM );
    CHECK_HR ( pOut->Rename ( MSGF_SCOPE_CHILD, L"letter", L"a.b" ), MSGF_E_NAME );
    CHECK_HR ( pOut->Retype ( MSGF_SCOPE_CHILD, L"ghost", MSGF_TYPE_INT32 ), MSGF_E_NO_ITEM );
    CHECK_HR ( pOut->Move   ( MSGF_SCOPE_CHILD, L"ghost", pIn ),     MSGF_E_NO_ITEM );
    CHECK_HR ( pOut->Move   ( MSGF_SCOPE_CHILD, L"letter", 0 ),      E_POINTER );

    pOut->Release ( ); pIn->Release ( ); pRoot->Release ( ); pStore->Release ( );
}


// =========================================================================
// 6. Change notification
// =========================================================================
//
// The C API took a function pointer and a void* user token, which is what a C
// caller has. A vtable ABI takes an INTERFACE the client implements -- one
// method, no macros -- and every language that can call this ABI can also
// implement one.
//
// The delivery contract is the kernel's, unchanged and stated once in the
// header: synchronous, on the mutating thread, with the store's lock HELD.
//
class Capture : public IMsgStoreEvents
{
    public:
      virtual void OnTrigger ( unsigned int type, unsigned long long pos )
      {
        ++m_nCount;
        m_uLastType = type;
        m_uLastPos  = pos;
      }
      int                m_nCount    = 0;
      unsigned int       m_uLastType = 0;
      unsigned long long m_uLastPos  = 0;
};

static void Demo_Triggers ( IMsgLibrary *pLib )
{
    light::Section ( L"6. headless change notification" );

    IMsgStore *pStore = NewStore ( pLib );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgNode *pWatched = 0;
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Watched", L"v", 0, &pWatched );
    unsigned long long uPos = 0;
    CHECK_HR ( pWatched->GetPos ( &uPos ), S_OK );
    CHECK ( uPos != 0 );

    Capture oCap;
    CHECK_HR ( pStore->SetEvents ( &oCap ), S_OK );
    CHECK_HR ( pStore->Arm ( MSGF_TRIG_UPDATE | MSGF_TRIG_INSERT, uPos ), S_OK );

    unsigned int uFired = 0;
    CHECK_HR ( pStore->Fire ( MSGF_TRIG_UPDATE, uPos, &uFired ), S_OK );
    CHECK ( uFired == 1 );
    CHECK ( oCap.m_nCount == 1 );
    CHECK ( oCap.m_uLastType == MSGF_TRIG_UPDATE );
    CHECK ( oCap.m_uLastPos == uPos );

    CHECK_HR ( pStore->Fire ( MSGF_TRIG_INSERT, uPos, &uFired ), S_OK );
    CHECK ( uFired == 1 );
    CHECK ( oCap.m_nCount == 2 );

    // Disarm one leg; the other survives.
    CHECK_HR ( pStore->Disarm ( MSGF_TRIG_UPDATE, uPos ), S_OK );
    CHECK_HR ( pStore->Fire ( MSGF_TRIG_UPDATE, uPos, &uFired ), S_OK );
    CHECK ( uFired == 0 );
    CHECK ( oCap.m_nCount == 2 );

    // Clearing the sink BEFORE it dies is the caller's job, and the header
    // says so: nothing here can tell a freed sink from a live one.
    CHECK_HR ( pStore->SetEvents ( 0 ), S_OK );
    wprintf ( L"  sink saw %d notifications\n", oCap.m_nCount );

    pWatched->Release ( ); pRoot->Release ( ); pStore->Release ( );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    light::InitConsole ( );
    wprintf ( L"=== MgrCApiTest (Light) - the store through a flat vtable ABI ===\n" );

    // The ABI gate. `abiVersion` MUST be the constant this header defines: the
    // DLL compares it against the range it still serves and refuses rather
    // than handing back an object whose vtable the caller would read at the
    // wrong offsets. Msgcore_c.h had no equivalent -- a stale binding against
    // it simply misbehaved.
    IMsgLibrary *pLib = 0;
    CHECK_HR ( MSGF_CreateLibrary ( 0, &pLib ), MSGF_E_ABI_MISMATCH );
    CHECK ( pLib == 0 );
    CHECK_HR ( MSGF_CreateLibrary ( ABI_VERSION, 0 ), E_POINTER );

    if ( FAILED ( MSGF_CreateLibrary ( ABI_VERSION, &pLib ) ) || !pLib )
    {
      wprintf ( L"FATAL: no library; nothing else can run.\n" );
      return light::EXIT_SETUP;
    }
    wprintf ( L"%s\n", pLib->VersionString ( ) );

    Demo_OneKindOfHandle ( pLib );
    Demo_TypedValues     ( pLib );
    Demo_Navigate        ( pLib );
    Demo_PosAndPersist   ( pLib );
    Demo_StructuralEdits ( pLib );
    Demo_Triggers        ( pLib );

    pLib->Release ( );
    return light::Verdict ( );
}
