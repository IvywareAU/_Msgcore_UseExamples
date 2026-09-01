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
// BstrWidthTestCom.cpp
//
// The counterpart of ..\DirectExamples\BstrWidthTest: the HEAP under
// everything else, and the one decision about it that a caller has to make and
// can never change afterwards.
//
// P3PmsgBSTR itself is not here -- it carries no entry point in Msgcore_c.h, so
// this server cannot see it -- and that turns out to matter less than it
// sounds. What P3PmsgBSTR exposes to a caller who is not writing Msgcore is its
// ADDRESSING WIDTH, and the width is a property of the STORE:
//
//      P2PmsgMgr16 / 32 / 64            store.CreateNew msgcAddr16 / 32 / 64
//      P3PmsgField16                    a node of a 16-bit store
//
// -- which is arguably the more honest spelling. In C++ the width is baked into
// a TYPE NAME, so a function that takes a P3PmsgField16 cannot be handed a node
// of a 64-bit store even though the two are identical to use; here a node is a
// node and the width lives where the decision was actually made.
//
// THIS FILE USED TO CLAIM A SECOND ABSENCE -- the paging callbacks and
// SafeRegistrationPush -- on the grounds that there is no automation type for a
// function pointer. That was an answer to the wrong question: a paging SINK is
// an object, not a function pointer, and Msgcore_c.h has since grown
// msgcore_mgr_set_paging_sinks. MgrPersistTestCom drives the whole facility.
// Section 5 here is now the other half of the same subject: bounding a heap
// with CreateNew's two sizes, which needs no sink at all.
//
// Exit codes: 0 SUCCESS  1 SETUP  2 ASSERT  3 a check failed.

#include "../common/ComHarness.h"

using namespace msgc;


// The same tree every time, so the three widths are compared on identical
// content and any difference in Size is the ADDRESSING and nothing else.
static void FillSampleTree ( Store& store, int nLeaves )
{
    Field root = store.root();
    Field cfg;
    CHECK ( SUCCEEDED ( root.declare ( L"cfg", Var ( 0 ), &cfg ) ) );

    for ( int i = 0; i < nLeaves; ++i )
    {
        WCHAR wszName[32];
        ::swprintf_s ( wszName, L"key%03d", i );
        CHECK ( SUCCEEDED ( cfg.declare ( wszName, Var ( i * 7 ) ) ) );
    }
}

static bool VerifySampleTree ( Store& store, int nLeaves )
{
    if ( store.fieldAt ( L".cfg" ).count() != nLeaves ) return false;
    for ( int i = 0; i < nLeaves; ++i )
    {
        WCHAR wszPath[64];
        ::swprintf_s ( wszPath, L".cfg.key%03d", i );
        if ( store.fieldAt ( wszPath ).asLong() != i * 7 ) return false;
    }
    return true;
}


// =========================================================================
// 1. The heap under everything else
// =========================================================================
static void Demo_Heap ( )
{
    Section ( L"1. The heap -- allocation, growth, and what Size means" );

    Store store;
    CHECK ( store.ok() );

    // A default store: whatever msgcore_mgr_create makes.
    const LONG cbEmpty = store.size();
    CHECK ( cbEmpty > 0 );
    Note ( L"default heap: %d bytes empty", cbEmpty );

    // SIZE IS ALLOCATION, NOT OCCUPANCY, and this is the cleanest place to see
    // it: a handful of nodes do not move the number at all, because they fit in
    // what was already reserved.
    FillSampleTree ( store, 5 );
    CHECK ( store.size() == cbEmpty );
    Note ( L"5 nodes     : still %d bytes -- they fit in the initial reservation",
           store.size() );

    // Outgrow it, and it moves.
    Field cfg = store.fieldAt ( L".cfg" );
    for ( int i = 0; i < 400; ++i )
    {
        WCHAR wszName[32];
        ::swprintf_s ( wszName, L"pad%03d", i );
        CHECK ( SUCCEEDED ( cfg.declare ( wszName, Var ( L"padding padding padding padding" ) ) ) );
    }
    const LONG cbGrown = store.size();
    CHECK ( cbGrown > cbEmpty );
    Note ( L"405 nodes   : %d bytes", cbGrown );

    // AND IT DOES NOT SHRINK. Truncate frees the nodes back into the heap's own
    // free list, not back to the process -- which is exactly what makes the
    // heap relocation-safe and a Save a plain block write. Worth pinning,
    // because "I deleted everything and Size did not change" reads as a leak
    // and is not one.
    CHECK ( SUCCEEDED ( cfg.truncate() ) );
    CHECK ( cfg.count() == 0 );
    CHECK ( store.size() == cbGrown );
    Note ( L"after Truncate: still %d bytes -- freed into the heap, not the process",
           store.size() );
}


// =========================================================================
// 2. The addressing width, chosen once
// =========================================================================
//
// The width decides how wide every internal reference in the heap is, and
// therefore the on-disk layout. It is fixed for a store's lifetime, which is
// why CreateNew is the only place it can be set -- there is no property for it,
// because a property implies it could be assigned.
//
static void Demo_Widths ( )
{
    Section ( L"2. msgcAddr16 / 32 / 64 -- fixed once, at CreateNew" );

    const LONG widths[] = { ADDR_16, ADDR_32, ADDR_64 };
    const LPCWSTR names[] = { L"Addr16", L"Addr32", L"Addr64" };

    for ( int i = 0; i < 3; ++i )
    {
        Store s;
        CHECK ( s.ok() );
        CHECK ( SUCCEEDED ( s.createNew ( widths[i], 0, 0 ) ) );
        CHECK ( s.isValid() );

        // A store of any width is a store: the whole API is width-agnostic,
        // which is the claim C++'s P3PmsgField16 / 32 / 64 typedefs make by
        // being interchangeable in everything but their names.
        FillSampleTree ( s, 20 );
        CHECK ( VerifySampleTree ( s, 20 ) );
        CHECK ( s.root().count() == 1 );
        CHECK ( s.fieldAt ( L".cfg.key007" ).asLong() == 49 );
        CHECK ( s.fieldAt ( L".cfg.key007" ).typeName() == L"INT32" );

        Note ( L"%s      : 20 nodes, heap %d bytes", names[i], s.size() );
    }

    // An unrecognised mode is refused rather than silently defaulted -- a
    // store built at the wrong width is a file nobody can read back.
    Store bad;
    CHECK ( bad.ok() );
    CHECK ( bad.createNew ( 0, 0, 0 ) == E_INVALIDARG );
    CHECK ( bad.createNew ( 9, 0, 0 ) == E_INVALIDARG );
    ShowError ( L"CreateNew(9)", E_INVALIDARG );

    // ... and the store it was called on is untouched, because CreateNew builds
    // the replacement BEFORE destroying what is there.
    CHECK ( bad.isValid() );
    CHECK ( bad.root().ok() );

    // Explicit initial and maximum sizes. The initial one is observable
    // immediately, which is the whole reason to pass it: a store that is going
    // to hold a megabyte should not grow to it in fifty steps.
    Store sized;
    CHECK ( sized.ok() );
    CHECK ( SUCCEEDED ( sized.createNew ( ADDR_32, 32768, 1048576 ) ) );
    CHECK ( sized.size() >= 32768 );
    Note ( L"Addr32 with an explicit 32 KB reservation: %d bytes empty", sized.size() );

    FillSampleTree ( sized, 50 );
    CHECK ( VerifySampleTree ( sized, 50 ) );
    CHECK ( sized.size() >= 32768 );
}


// =========================================================================
// 3. The same tree at three widths, and through a file
// =========================================================================
//
// The interesting claim is not that each width works on its own -- it is that
// the WIDTH IS PART OF THE FILE, and that a reader does not have to be told
// which one it is getting.
//
static void Demo_ThreeWidthsThroughAFile ( )
{
    Section ( L"3. The same tree at three widths, saved and read back" );

    const LONG widths[] = { ADDR_16, ADDR_32, ADDR_64 };
    const LPCWSTR names[] = { L"Addr16", L"Addr32", L"Addr64" };
    const LPCWSTR files[] = { L"BstrWidthTestCom16.p2p",
                              L"BstrWidthTestCom32.p2p",
                              L"BstrWidthTestCom64.p2p" };
    LONG cbOnDisk[3] = { 0, 0, 0 };

    for ( int i = 0; i < 3; ++i )
    {
        const std::wstring path = TempFile ( files[i] );
        ::DeleteFileW ( path.c_str() );

        {
            Store s;
            CHECK ( s.ok() );
            CHECK ( SUCCEEDED ( s.createNew ( widths[i], 0, 0 ) ) );
            FillSampleTree ( s, 40 );
            CHECK ( SUCCEEDED ( s.save ( path.c_str() ) ) );
            CHECK ( s.filename() == path );
        }

        // Sizes, so the widths are visibly different things on disk.
        HANDLE h = ::CreateFileW ( path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
        CHECK ( h != INVALID_HANDLE_VALUE );
        if ( h != INVALID_HANDLE_VALUE )
        {
            cbOnDisk[i] = (LONG)::GetFileSize ( h, NULL );
            ::CloseHandle ( h );
        }

        // A PLAIN, DEFAULT STORE READS IT. The reader is not told the width and
        // does not have to be: it is in the image. That is what makes a .p2p
        // portable between a 16-bit writer and a 64-bit reader.
        Store reader;
        CHECK ( reader.ok() );
        CHECK ( SUCCEEDED ( reader.open ( path.c_str() ) ) );
        CHECK ( VerifySampleTree ( reader, 40 ) );
        CHECK ( reader.fieldAt ( L".cfg.key039" ).asLong() == 273 );

        Note ( L"%s      : %d bytes on disk, read back by a default store",
               names[i], cbOnDisk[i] );

        ::DeleteFileW ( path.c_str() );
    }

    // The narrow store really is narrower -- if all three were the same size,
    // the mode would be doing nothing.
    CHECK ( cbOnDisk[0] > 0 && cbOnDisk[1] > 0 && cbOnDisk[2] > 0 );
    CHECK ( cbOnDisk[0] < cbOnDisk[2] );
}


// =========================================================================
// 4. There is no P3PmsgField16 here, and that is the point
// =========================================================================
static void Demo_NoWidthInTheType ( )
{
    Section ( L"4. A node is a node -- the width is not in its type" );

    Store narrow, wide;
    CHECK ( narrow.ok() && wide.ok() );
    CHECK ( SUCCEEDED ( narrow.createNew ( ADDR_16, 0, 0 ) ) );
    CHECK ( SUCCEEDED ( wide.createNew   ( ADDR_64, 0, 0 ) ) );

    FillSampleTree ( narrow, 10 );
    FillSampleTree ( wide,   10 );

    // ONE FUNCTION, BOTH STORES. In C++ this is where a caller discovers that
    // P3PmsgField16 and P3PmsgField64 are different types and their code has to
    // be a template or duplicated; here the interface pointer is the same
    // interface pointer and the width never appears in a signature.
    CHECK ( VerifySampleTree ( narrow, 10 ) );
    CHECK ( VerifySampleTree ( wide,   10 ) );

    Field a = narrow.fieldAt ( L".cfg.key003" );
    Field b = wide.fieldAt   ( L".cfg.key003" );
    CHECK ( a.ok() && b.ok() );
    CHECK ( a.asLong() == b.asLong() );
    CHECK ( a.typeName() == b.typeName() );
    CHECK ( a.path() == b.path() );

    // A node of one store and a node of another are still different objects
    // over different heaps, and MoveChild is where that has to be enforced
    // rather than merely believed.
    //
    // A node reference here is a PATH, so a destination from ANOTHER store
    // would have its path resolved in THIS one -- and these two stores have the
    // same shape, so "cfg" resolves in both. Without an identity check the move
    // would succeed, silently, against the wrong tree (here, against itself).
    // So it is refused, and msgcForeign is the code to branch on.
    Field nCfg = narrow.fieldAt ( L".cfg" );
    Field wCfg = wide.fieldAt ( L".cfg" );
    CHECK ( nCfg.ok() && wCfg.ok() );

    const LONG cNarrowBefore = nCfg.count();
    const LONG cWideBefore   = wCfg.count();

    CHECK ( nCfg.moveChildHr ( wCfg, L"key003" ) == E_FOREIGN );
    ShowError ( L"MoveChild into another store", E_FOREIGN );

    CHECK ( wide.fieldAt   ( L".cfg" ).count() == cWideBefore );
    CHECK ( narrow.fieldAt ( L".cfg" ).count() == cNarrowBefore );
    CHECK ( narrow.fieldAt ( L".cfg.key003" ).ok() );
    CHECK ( wide.fieldAt   ( L".cfg.key003" ).ok() );
    Note ( L"a cross-store MoveChild is refused, and neither store moved" );

    // A move to where the child already is answers S_FALSE and changes
    // nothing -- the flat move is a remove-and-re-add, so performing it would
    // churn the child's position for no reason.
    CHECK ( nCfg.moveChildHr ( nCfg, L"key003" ) == S_FALSE );
    CHECK ( narrow.fieldAt ( L".cfg" ).count() == cNarrowBefore );

    // A real move WITHIN one store still works, which is what makes the two
    // refusals above meaningful rather than a blanket ban.
    Field nRoot = narrow.root();
    CHECK ( nRoot.ok() );
    CHECK ( nCfg.moveChild ( nRoot, L"key003" ) );
    CHECK ( narrow.fieldAt ( L".cfg" ).count() == cNarrowBefore - 1 );
    CHECK ( narrow.fieldAt ( L".key003" ).ok() );
    CHECK ( narrow.fieldAt ( L".key003" ).asLong() == 21 );
}


// =========================================================================
// 5. Bounding a heap -- and the paging hooks, which are no longer absent
// =========================================================================
//
// This section used to record that the paging hooks did not cross: they let a
// host supply its own backing for a heap by handing Msgcore FUNCTION POINTERS,
// and there is no VARIANT type for one. That reasoning was about the wrong
// thing. A sink OBJECT is not a function pointer; the flat ABI grew
// msgcore_mgr_set_paging_sinks, and MgrPersistTestCom's paging section now
// drives the whole facility -- SetPagingSink, PageIn, PageOut, PushPaging --
// including the SafeRegistrationPush guard, which is PushPaging/PopPaging here.
//
// What is left in THIS file is the other half of the same question, and it is
// the half most callers actually want: bounding a heap without writing a sink
// at all. CreateNew's two size arguments say how much backing a store should
// have and how much it may ever take, and unlike a paging handler they cost the
// client nothing to get right and cannot deadlock the store.
//
static void Demo_BoundedHeap ( )
{
    Section ( L"5. Bounding a heap without a sink -- CreateNew's two sizes" );

    // The maximum is the part that a paging hook was really being used for:
    // bounding a heap. Set it, and the store simply will not grow past it.
    Store bounded;
    CHECK ( bounded.ok() );
    CHECK ( SUCCEEDED ( bounded.createNew ( ADDR_32, 4096, 65536 ) ) );
    CHECK ( bounded.size() >= 4096 );

    Field cfg;
    CHECK ( SUCCEEDED ( bounded.root().declare ( L"cfg", Var ( 0 ), &cfg ) ) );

    // Push until it either fills or refuses. Whichever happens, the store is
    // still usable afterwards and says so -- which is the property that
    // matters to a client and the one a raw paging callback would have made
    // the client responsible for.
    int nStored = 0;
    bool bRefused = false;
    for ( int i = 0; i < 4000 && !bRefused; ++i )
    {
        WCHAR wszName[32];
        ::swprintf_s ( wszName, L"k%04d", i );
        if ( FAILED ( cfg.declare ( wszName, Var ( L"0123456789012345678901234567890123456789" ) ) ) )
            bRefused = true;
        else
            ++nStored;
    }

    Note ( L"bounded at 64 KB: stored %d nodes, heap %d bytes, refused=%s",
           nStored, bounded.size(), bRefused ? L"yes" : L"no" );

    CHECK ( nStored > 0 );
    CHECK ( bounded.isValid() );
    CHECK ( bounded.root().ok() );

    // Whatever it did store is intact and readable -- a bounded heap is not a
    // corrupted one.
    CHECK ( bounded.fieldAt ( L".cfg" ).count() == nStored );
    CHECK ( bounded.fieldAt ( L".cfg.k0000" ).asText().length() == 40 );

    // And it still saves and reloads, which is the real test of "still usable".
    const std::wstring path = TempFile ( L"BstrWidthTestComBounded.p2p" );
    ::DeleteFileW ( path.c_str() );
    CHECK ( SUCCEEDED ( bounded.save ( path.c_str() ) ) );

    Store back;
    CHECK ( back.ok() );
    CHECK ( SUCCEEDED ( back.open ( path.c_str() ) ) );
    CHECK ( back.fieldAt ( L".cfg" ).count() == nStored );
    ::DeleteFileW ( path.c_str() );

    Note ( L"The paging sink is no longer the missing half of this: see" );
    Note ( L"MgrPersistTestCom, which drives it and PushPaging/PopPaging." );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    InitConsole();

    wprintf ( L"=== BstrWidthTestCom - the heap, and its one irreversible decision ===\n" );
    fflush ( stdout );

    Apartment apt;
    if ( !apt.ok() ) return SetupFailure ( L"CoInitializeEx", E_FAIL );

    {
        // Proves the server is reachable before any section runs, so a missing
        // registration is exit 1 and not a wall of failed checks.
        Store probe;
        if ( !probe.ok() )
            return SetupFailure ( L"CoCreateInstance(MsgcoreCom.MsgStore)", probe.hr() );
        Log ( L"MAIN", L"%s", probe.versionString().c_str() );
    }

    Demo_Heap                   ( );
    Demo_Widths                 ( );
    Demo_ThreeWidthsThroughAFile( );
    Demo_NoWidthInTheType       ( );
    Demo_BoundedHeap            ( );

    return Verdict();
}
