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
// MgrPersistTestCom.cpp
//
// The counterpart of ..\DirectExamples\MgrPersistTest: a P2PmsgMgr
// as a DOCUMENT -- one heap owning one tree, saved, loaded, addressed by
// position, and watched for changes.
//
// Three things are shaped differently here, and each is a consequence of what
// COM is rather than a liberty taken:
//
//  * A STORE IS THE CREATABLE OBJECT. CoCreateInstance gives you an empty one,
//    so `New-Object -ComObject MsgcoreCom.MsgStore` is the whole of "open a
//    document". Each one is independent -- unlike TargetCom's P2PNetwork, which
//    wraps a process-wide singleton -- because a store IS a document and nothing
//    about two of them interferes.
//
//  * IT OWNS EVERYTHING IT HANDS OUT, transitively. Fields, collections and
//    cursors all hold a counted reference to it, so the heap cannot be freed
//    while anything still points into it, however careless the client is about
//    release order. Close() is therefore about WHEN, not whether -- and after
//    it, everything derived answers msgcClosed instead of reading freed memory.
//
//  * TRIGGERS ARE QUEUED, NOT CALLED BACK. The flat sink runs on the thread
//    performing the mutation, which is holding this store's lock and is in the
//    middle of a heap mutation; firing a client handler from there would
//    deadlock the first one that touched the store. So every fired trigger is
//    copied and replayed on a dispatch thread, marshalled into this apartment.
//    Which means THIS PROGRAM MUST PUMP -- see Gate::wait().
//
// Exit codes: 0 SUCCESS  1 SETUP  2 ASSERT  3 a check failed.

#include "../common/ComHarness.h"

using namespace msgc;


// =========================================================================
// 1. A store is a heap that owns a tree
// =========================================================================
static void Demo_Store ( Store& store )
{
    Section ( L"1. MsgStore -- one heap, one tree" );

    CHECK ( store.isValid() );
    CHECK ( store.filename().empty() );             // never opened or saved yet
    CHECK ( !store.rootName().empty() );

    Note ( L"root name   : '%s'", store.rootName().c_str() );
    Note ( L"heap size   : %d bytes", store.size() );

    // The root is a node like any other -- which is the model's whole claim.
    Field root = store.root();
    CHECK ( root.ok() );
    CHECK ( root.path().empty() );                  // the root is the empty path
    CHECK ( root.count() == 0 );

    // THE ROOT'S NAME USED TO BE READ-ONLY AT THIS TIER, and the reason is worth
    // keeping because the kernel's spelling actively suggests otherwise:
    // P2PmsgMgr::Rename sits among the serialisation methods and reads like a
    // rename of the store, but it is a MoveFileEx on m_strFilename. So it renamed
    // the FILE, it took a PATH, and it answered FALSE on a store that had never
    // been saved -- and this tier published it as RenameFile to say so.
    //
    // It is GONE. A host has its own file API, and what a client actually wanted
    // was the inverse of RootName, which is what RenameRoot is.
    CHECK ( store.rootName() == L"P2PmsgMgr name" );     // the default
    CHECK ( SUCCEEDED ( store.renameRoot ( L"Settings" ) ) );
    CHECK ( store.rootName() == L"Settings" );

    // It is validated like any other name: 1 to 63 UTF-16 units, none of the
    // path grammar's separators.
    CHECK ( store.renameRoot ( L"" ) == E_NAME );
    CHECK ( store.renameRoot ( L"has.a.dot" ) == E_NAME );
    ShowError ( L"RenameRoot with a separator in the name", E_NAME );
    CHECK ( store.rootName() == L"Settings" );           // refused, not half-done

    // ... and IsValidName asks the same question WITHOUT writing anything, which
    // is what a form validating user input wants.
    CHECK ( store.isValidName ( L"Settings" ) );
    CHECK ( !store.isValidName ( L"has.a.dot" ) );
    CHECK ( !store.isValidName ( L"" ) );
    CHECK ( store.isValidName ( std::wstring ( 63, L'a' ).c_str() ) );
    CHECK ( !store.isValidName ( std::wstring ( 64, L'a' ).c_str() ) );

    CHECK ( SUCCEEDED ( store.renameRoot ( L"P2PmsgMgr name" ) ) );   // put it back

    // Build something worth saving.
    Field win;
    CHECK ( SUCCEEDED ( root.declare ( L"window", Var ( 0 ), &win ) ) );
    CHECK ( SUCCEEDED ( win.declare ( L"width",  Var ( 1024 ) ) ) );
    CHECK ( SUCCEEDED ( win.declare ( L"height", Var ( 768 ) ) ) );
    CHECK ( SUCCEEDED ( win.declare ( L"title",  Var ( L"Ivyware Chartboard" ) ) ) );

    Field net;
    CHECK ( SUCCEEDED ( root.declare ( L"network", Var ( 0 ), &net ) ) );
    CHECK ( SUCCEEDED ( net.declare ( L"host", Var ( L"127.0.0.1" ) ) ) );
    CHECK ( SUCCEEDED ( net.declareTyped ( L"port", Var ( 7788 ), TYPE_UINT16 ) ) );

    CHECK ( root.count() == 2 );
    CHECK ( store.fieldAt ( L".window.width" ).asLong() == 1024 );
    CHECK ( store.fieldAt ( L".network.port" ).typeName() == L"UINT16" );

    // SIZE IS ALLOCATION, NOT OCCUPANCY. It starts at the heap's initial size
    // and only moves when the tree outgrows it, so a small tree in a 4 KB heap
    // reports 4 KB before and after. Reported as it is rather than
    // reinterpreted: a store's footprint is the honest answer to "how big".
    const LONG cbBefore = store.size();
    Field bulk;
    CHECK ( SUCCEEDED ( root.declare ( L"bulk", Var ( 0 ), &bulk ) ) );
    for ( int i = 0; i < 200; ++i )
    {
        WCHAR wszName[32];
        ::swprintf_s ( wszName, L"k%03d", i );
        CHECK ( SUCCEEDED ( bulk.declare ( wszName, Var ( L"a reasonably long value string" ) ) ) );
    }
    const LONG cbAfter = store.size();
    CHECK ( cbAfter >= cbBefore );
    Note ( L"heap grew   : %d -> %d bytes for 200 more nodes", cbBefore, cbAfter );

    CHECK ( SUCCEEDED ( bulk.truncate() ) );
    CHECK ( bulk.count() == 0 );
    CHECK ( root.remove ( L"bulk" ) );
}


// =========================================================================
// 2. Save / Load -- the store as a document
// =========================================================================
static void Demo_SaveLoad ( Store& store )
{
    Section ( L"2. Save / Load -- the store as a document" );

    const std::wstring file = TempFile ( L"MgrPersistTestCom.p2p" );
    ::DeleteFileW ( file.c_str() );

    // Dirty is a flag the client can also drive, because "has this changed"
    // is a question only the client can finally answer.
    CHECK ( SUCCEEDED ( store.setDirty ( true ) ) );
    CHECK ( store.dirty() );

    // Save with no path on a store that has never been given one is msgcSave,
    // not a file called "" somewhere.
    CHECK ( store.save ( L"" ) == E_SAVE );
    ShowError ( L"Save() with no path and no Filename", E_SAVE );

    CHECK ( SUCCEEDED ( store.save ( file.c_str() ) ) );
    CHECK ( store.filename() == file );
    CHECK ( ::GetFileAttributesW ( file.c_str() ) != INVALID_FILE_ATTRIBUTES );
    Note ( L"saved to    : %s", file.c_str() );

    // Now the no-argument form writes back over Filename.
    CHECK ( SUCCEEDED ( store.root().child ( L"window" ).child ( L"width" ).setValue ( Var ( 1280 ) ) ) );
    CHECK ( SUCCEEDED ( store.save() ) );

    // A SECOND, INDEPENDENT DOCUMENT. Two CoCreateInstances share nothing --
    // no heap, no lock, no event queue.
    Store other;
    CHECK ( other.ok() );
    CHECK ( other.root().count() == 0 );
    CHECK ( SUCCEEDED ( other.open ( file.c_str() ) ) );

    CHECK ( other.rootName() == store.rootName() );
    CHECK ( other.root().count() == 2 );
    CHECK ( other.fieldAt ( L".window.width" ).asLong() == 1280 );
    CHECK ( other.fieldAt ( L".window.title" ).asText() == L"Ivyware Chartboard" );
    CHECK ( other.fieldAt ( L".network.host" ).asText() == L"127.0.0.1" );

    // THE DECLARED WIDTH SURVIVED THE ROUND TRIP, which is the whole reason
    // DeclareTyped exists: a config round trip that normalised UINT16 to INT32
    // would have rewritten the file just by reading and re-saving it.
    CHECK ( other.fieldAt ( L".network.port" ).typeName() == L"UINT16" );
    CHECK ( other.fieldAt ( L".network.port" ).asLong() == 7788 );

    // The two are genuinely independent afterwards.
    CHECK ( SUCCEEDED ( other.fieldAt ( L".window.width" ).setValue ( Var ( 640 ) ) ) );
    CHECK ( other.fieldAt ( L".window.width" ).asLong() == 640 );
    CHECK ( store.fieldAt ( L".window.width" ).asLong() == 1280 );

    // Clear -- was Nullify -- empties a store and keeps it open, which is
    // distinct from Close, which destroys it.
    //
    // TWO THINGS ABOUT IT CHANGED. It KEEPS ITS FILENAME now, because it is the
    // same store rather than a rebuilt one, so a "New" command in a host does not
    // silently lose the document it was editing. And a node reference a client is
    // holding stays valid and reports msgcNoField, rather than pointing into a
    // store that no longer exists.
    //
    // (The rebuild still happens; it happens one layer down, once for every
    // client. The core's own Nullify closes the heap and leaves the manager
    // pointing at nothing while still answering is_valid, so nothing may call it
    // directly -- the next call through it access-violates.)
    Field heldBefore = other.fieldAt ( L".window.width" );
    CHECK ( heldBefore.ok() );

    CHECK ( SUCCEEDED ( other.clear() ) );
    CHECK ( other.isValid() );
    CHECK ( other.root().ok() );
    CHECK ( other.root().count() == 0 );
    CHECK ( other.filename() == file );              // kept, unlike the old Nullify

    // msgcSTALE, not msgcNoField: the two are different questions and this tier
    // keeps them apart. msgcNoField is "the name you asked me to look up is not
    // there"; msgcStale is "the node THIS OBJECT names is gone". A reference
    // held across a Clear is the second, and the sentence a client reads says so.
    Var v;
    CHECK ( heldBefore.raw()->get_Value ( v.addr() ) == E_STALE );
    ShowError ( L"a node reference held across Clear", E_STALE );

    // And the store is genuinely usable, not merely valid-looking.
    CHECK ( SUCCEEDED ( other.root().declare ( L"afterClear", Var ( 1 ) ) ) );
    CHECK ( other.root().count() == 1 );

    // A file that is not a Msgcore image is refused at its synchronisation
    // word rather than half-loaded.
    const std::wstring junk = TempFile ( L"MgrPersistTestCom.junk" );
    HANDLE h = ::CreateFileW ( junk.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL );
    if ( h != INVALID_HANDLE_VALUE )
    {
        DWORD cb = 0;
        ::WriteFile ( h, "not a p2p image at all, not even close", 37, &cb, NULL );
        ::CloseHandle ( h );

        Store bad;
        CHECK ( bad.ok() );
        CHECK ( bad.open ( junk.c_str() ) == E_LOAD );
        ShowError ( L"Open(a text file)", E_LOAD );
        ::DeleteFileW ( junk.c_str() );
    }

    // A path that does not exist is the same code, and says the same thing.
    Store missing;
    CHECK ( missing.open ( L"Z:\\no\\such\\place\\nothing.p2p" ) == E_LOAD );

    ::DeleteFileW ( file.c_str() );
}


// =========================================================================
// 3. P2Pos -- addressing a node by position
// =========================================================================
//
// A P2Pos is the identity MSGCORE ITSELF uses: it is what a trigger carries,
// and it is the only thing a DELETED node still has. Both identities this layer
// publishes come back through FieldAt.
//
// THIS SECTION USED TO BE ABOUT AN ASYMMETRY, and the asymmetry is gone. A path
// answered a writable node; a P2Pos answered a DETACHED COPY whose writes
// vanished, because the flat ABI's p2pos2field deep-copies. Both spellings now
// arrive at the same kind of thing, because a position is searched for once and
// answered as a ROUTE. Two consequences follow, and both are checked below: a
// P2Pos is writable, and a P2Pos whose node has been deleted is a clean
// msgcNoPos rather than a debug break on a freed block.
//
static void Demo_P2Pos ( Store& store )
{
    Section ( L"3. P2Pos -- the identity Msgcore itself uses" );

    Field width = store.fieldAt ( L".window.width" );
    CHECK ( width.ok() );

    const LONGLONG pos = width.p2pos();
    CHECK ( pos != 0 );
    Note ( L".window.width: P2Pos=%lld", pos );

    // It is STABLE across unrelated mutations -- that is what makes it an inode.
    CHECK ( SUCCEEDED ( store.root().declare ( L"unrelated", Var ( 1 ) ) ) );
    CHECK ( store.fieldAt ( L".window.width" ).p2pos() == pos );

    // PATHOF ROUND-TRIPS NOW, which it did not. It used to answer MSGCORE's own
    // path spelling -- a leading '.' and the root's NAME as the first segment --
    // so the two spellings looked similar enough to be mistaken for each other
    // and feeding this one back to FieldAt found nothing. The help string had to
    // say so. Both ends are the same grammar, so a path handed out can be handed
    // straight back.
    const std::wstring own = store.pathOf ( pos );
    CHECK ( own == L".window.width" );
    CHECK ( own == width.path() );
    Note ( L"PathOf      : '%s'  == Field.Path, and FieldAt takes it back",
           own.c_str() );

    Field byOwnPath;
    CHECK ( SUCCEEDED ( store.fieldAt ( own.c_str(), byOwnPath ) ) );
    CHECK ( byOwnPath.p2pos() == pos );

    // A STRING resolves the node.
    Field live;
    CHECK ( SUCCEEDED ( store.fieldAt ( L".window.width", live ) ) );
    CHECK ( SUCCEEDED ( live.setValue ( Var ( 1600 ) ) ) );
    CHECK ( store.fieldAt ( L".window.width" ).asLong() == 1600 );

    // A NUMBER resolves the SAME node, and a write through it lands.
    Field byPos;
    CHECK ( SUCCEEDED ( store.fieldAt ( pos, byPos ) ) );
    CHECK ( byPos.asLong() == 1600 );
    CHECK ( byPos.path() == L".window.width" );
    CHECK ( SUCCEEDED ( byPos.setValue ( Var ( 1920 ) ) ) );
    CHECK ( store.fieldAt ( L".window.width" ).asLong() == 1920 );
    CHECK ( live.asLong() == 1920 );                 // one node, two references

    // A P2Pos from nowhere does not resolve, and neither does one whose node has
    // been deleted -- the search that resolves it walks the LIVE tree, so a freed
    // block is simply not found. That was the one sharp edge this layer could not
    // file off (it used to be a debug break), and it is filed off.
    Field gone;
    CHECK ( store.fieldAt ( (LONGLONG)0x7FFFFFFF, gone ) == E_NO_POS );
    ShowError ( L"FieldAt(a P2Pos from nowhere)", E_NO_POS );

    CHECK ( SUCCEEDED ( store.root().declare ( L"doomed", Var ( 1 ) ) ) );
    const LONGLONG doomedPos = store.fieldAt ( L".doomed" ).p2pos();
    CHECK ( doomedPos != 0 );
    CHECK ( store.root().remove ( L"doomed" ) );
    Field deleted;
    CHECK ( store.fieldAt ( doomedPos, deleted ) == E_NO_POS );

    // A path that does not resolve is a different code from a path that does not
    // PARSE, and both differ from "no such position". Three questions, three
    // answers.
    Field nope;
    CHECK ( store.fieldAt ( L".window.depth", nope ) == E_NO_FIELD );
    Field unparseable;
    CHECK ( store.fieldAt ( L"window.width", unparseable ) == E_PATH );
    ShowError ( L"FieldAt(\"window.width\") -- no leading separator", E_PATH );

    CHECK ( store.root().remove ( L"unrelated" ) );
}


// =========================================================================
// 4. Triggers -- headless change notification
// =========================================================================
//
// Arm a node by P2Pos and OnChange is raised for it. DELETE fires by itself
// when a node is freed; INSERT and UPDATE fire when a WRITER calls FireTrigger.
// That is the flat ABI's rule unchanged -- Msgcore does not detect its own
// mutations, it reports the ones it is told about -- and it is worth stating,
// because "I armed it and nothing happened" is otherwise a long afternoon.
//
// THE PATH TRAVELS WITH THE EVENT, and where it is RESOLVED moved with the port.
// It used to be looked up inside the callback, on the mutating thread, with the
// store mid-allocation -- which was cheap against a flat direct lookup and is an
// access violation against a depth-first search through a heap that is being
// relocated. The position is queued bare now and the path is resolved on the
// dispatch thread. The cost is accuracy for a node that disappears between the
// two moments; a DELETE never had a path anyway.
//
static void Demo_Triggers ( Store& store )
{
    Section ( L"4. Triggers -- headless change notification, replayed and marshalled" );

    CHECK ( SUCCEEDED ( store.advise() ) );

    Gate                    gate;
    std::vector<Change>     seen;
    std::vector<std::wstring> errors;

    store.sink()->onChange = [&] ( const Change& c ) { seen.push_back ( c ); gate.bump(); };
    store.sink()->onError  = [&] ( const wchar_t *w ) { errors.push_back ( w ? w : L"" ); };

    Field watched;
    CHECK ( SUCCEEDED ( store.root().declare ( L"watched", Var ( 100 ), &watched ) ) );
    const LONGLONG pos = watched.p2pos();
    CHECK ( pos != 0 );

    CHECK ( SUCCEEDED ( store.armTrigger ( TRIGGER_ALL, pos ) ) );

    // Nothing has been REPORTED yet, however much we mutate: Msgcore does not
    // detect its own writes.
    CHECK ( SUCCEEDED ( store.fieldAt ( L".watched", watched ) ) );
    CHECK ( SUCCEEDED ( watched.setValue ( Var ( 200 ) ) ) );
    PumpFor ( 200 );
    CHECK ( seen.empty() );
    Note ( L"a silent write: armed, mutated, 0 events -- FireTrigger is the report" );

    // The writer reports it. FireTrigger answers how many registrations ran,
    // which is the flat ABI's return value carried straight through.
    const LONG nFired = store.fireTrigger ( TRIGGER_UPDATE, pos );
    CHECK ( nFired >= 1 );

    // AND NOW WE MUST PUMP. The event was queued on the mutating thread and is
    // replayed on the store's dispatch thread, then marshalled into this STA;
    // a WaitForSingleObject here would hang for ever.
    CHECK ( gate.wait ( 1, 5000 ) );
    CHECK ( seen.size() == 1 );
    if ( seen.size() == 1 )
    {
        CHECK ( seen[0].kind == TRIGGER_UPDATE );
        CHECK ( seen[0].p2pos == pos );
        // The same spelling FieldAt takes, like every other path here.
        CHECK ( seen[0].path == L".watched" );
        Note ( L"OnChange    : kind=%d p2pos=%lld path='%s'",
               seen[0].kind, seen[0].p2pos, seen[0].path.c_str() );
    }

    // The handler ran on THIS thread, which is what the marshalling is for --
    // and it may call back into the store, because the dispatch thread does not
    // hold the store's lock while it fires.
    seen.clear();
    gate.reset();
    store.sink()->onChange = [&] ( const Change& c )
    {
        seen.push_back ( c );
        // Re-entrant read from inside the handler: this is the case that would
        // deadlock if the flat sink were called through directly.
        Field f;
        if ( SUCCEEDED ( store.fieldAt ( c.p2pos, f ) ) )
            CHECK ( f.asLong() == 200 );
        gate.bump();
    };
    CHECK ( store.fireTrigger ( TRIGGER_INSERT, pos ) >= 1 );
    CHECK ( gate.wait ( 1, 5000 ) );
    CHECK ( seen.size() == 1 );
    CHECK ( seen.size() == 1 && seen[0].kind == TRIGGER_INSERT );

    // DELETE is the one that fires BY ITSELF -- and the one with a rule
    // attached, which this section exists to state.
    //
    // The DELETE trigger is fired from inside P2PmsgHeap_FreeBSTRio
    // (MsgVBHeap.cpp:2847) AFTER the block has been freed and collated. So at
    // the only moment a path could have been captured, the node was already
    // gone: `path` is ALWAYS empty for a delete, and that is a fact about
    // Msgcore's ordering rather than an omission in the event.
    //
    // Which makes the P2Pos the whole of the answer -- and it is an IDENTITY to
    // COMPARE, not something to resolve. Handing it to FieldAt reaches
    // P2PmsgHeap_AssertValidAllocBSTRio (MsgVBHeap.cpp:1428), which finds a
    // freed block and trips ASSERT(0): a debug break in a Debug Msgcore and
    // undefined in a Release one. There is no "is this position still
    // allocated" predicate in the flat ABI, so no layer above it can check for
    // you. THIS HARNESS THEREFORE DOES NOT DO IT, deliberately.
    seen.clear();
    gate.reset();
    store.sink()->onChange = [&] ( const Change& c ) { seen.push_back ( c ); gate.bump(); };

    CHECK ( store.root().remove ( L"watched" ) );
    CHECK ( gate.wait ( 1, 5000 ) );
    CHECK ( !seen.empty() );
    if ( !seen.empty() )
    {
        CHECK ( seen[0].kind == TRIGGER_DELETE );
        CHECK ( seen[0].p2pos == pos );          // compare it -- do not resolve it
        CHECK ( seen[0].path.empty() );          // always, for a delete
        CHECK ( !store.root().exists ( L"watched" ) );
        Note ( L"OnChange    : kind=DELETE p2pos=%lld, path empty -- the block was",
               seen[0].p2pos );
        Note ( L"              already freed when the trigger fired" );
    }

    // Disarm, and the reports stop. (FireTrigger on an unarmed node is not an
    // error -- there is simply nothing registered to run.)
    Field again;
    CHECK ( SUCCEEDED ( store.root().declare ( L"watched2", Var ( 1 ), &again ) ) );
    const LONGLONG pos2 = again.p2pos();
    CHECK ( SUCCEEDED ( store.armTrigger ( TRIGGER_UPDATE, pos2 ) ) );
    CHECK ( SUCCEEDED ( store.disarmTrigger ( TRIGGER_UPDATE, pos2 ) ) );

    seen.clear();
    gate.reset();
    store.fireTrigger ( TRIGGER_UPDATE, pos2 );
    PumpFor ( 300 );
    CHECK ( seen.empty() );

    CHECK ( errors.empty() );
    store.unadvise();

    // After Unadvise nothing is delivered at all, which is the other half of
    // the connection-point contract.
    CHECK ( SUCCEEDED ( store.armTrigger ( TRIGGER_UPDATE, pos2 ) ) );
    seen.clear();
    store.fireTrigger ( TRIGGER_UPDATE, pos2 );
    PumpFor ( 200 );
    CHECK ( seen.empty() );
}


// =========================================================================
// main
// =========================================================================
// =========================================================================
// N. Paging -- the synchronous sink, and why it is not a connection point
// =========================================================================
//
// A store can delegate the residency of a subtree to its host, so it may
// describe far more data than it holds. The sink that does it is the opposite
// of the OnChange event sink at every point that matters:
//
//                    OnChange                 OnPageIn / OnPageOut
//   raised on        a dispatch thread        the ACCESSING thread
//   timing           after the fact           during, core is BLOCKED
//   store lock       not held                 HELD
//   return value     ignored                  the answer; False = failed
//   may block        yes                      no
//
// A page-in that has not returned is data that is not there, so there is no
// version of this that defers -- the queueing that makes OnChange safe cannot
// be applied. That is also why it is a directly-registered sink object rather
// than a source dispinterface: a dispinterface cannot usefully return a value
// across a marshalling boundary, and marshalling a call the core is
// synchronously waiting on, under a lock it already holds, is a deadlock.
//
static void Demo_Paging ( Store& store )
{
    Section ( L"N. Paging -- a synchronous sink, called with the core blocked" );

    Field f;
    CHECK ( SUCCEEDED ( store.root().declare ( L"paged", Var ( 0 ), &f ) ) );
    const LONGLONG pos = f.p2pos();
    CHECK ( pos != 0 );

    // With no sink installed, driving paging is a successful no-op rather than
    // an error: a store that does not page is the ordinary case.
    bool bOk = false;
    CHECK ( SUCCEEDED ( store.pageIn ( pos, &bOk ) ) );

    PagingSink *pSink = new PagingSink();
    CHECK ( SUCCEEDED ( store.setPagingSink ( pSink ) ) );

    CHECK ( SUCCEEDED ( store.pageIn ( pos, &bOk ) ) );
    CHECK ( bOk );
    CHECK ( pSink->PageIns() == 1 );

    // The sink ran BEFORE PageIn returned -- that is the whole contract, and
    // the count already being 1 here is the observable form of it.
    Note ( L"the sink had already run by the time PageIn returned" );

    CHECK ( SUCCEEDED ( store.pageOut ( pos, true, &bOk ) ) );
    CHECK ( bOk );
    CHECK ( pSink->PageOuts() == 1 );
    CHECK ( pSink->LastFlush() == 1 );          // the flush flag crossed intact

    CHECK ( SUCCEEDED ( store.pageOut ( pos, false, &bOk ) ) );
    CHECK ( pSink->LastFlush() == 0 );

    // The P2Pos crossed as a VARIANT and came back unchanged.
    CHECK ( pSink->LastPos().asInt64() == pos );

    // A sink that refuses is reported to the caller that provoked it, not
    // raised as an exception: the core cannot unwind through a page fault.
    pSink->Answer ( false );
    CHECK ( SUCCEEDED ( store.pageIn ( pos, &bOk ) ) );
    CHECK ( !bOk );
    pSink->Answer ( true );

    // --- suspend and restore --------------------------------------------------
    // What the C++ SafeRegistrationPush does with a constructor and destructor.
    // A Save is the usual reason: it walks everything and must not fault the
    // whole store in on the way past.
    pSink->Reset();
    CHECK ( SUCCEEDED ( store.pushPaging() ) );

    // Push does NOT nest. The core saves one registration, so a second push
    // would discard the first and the matching pop restore the wrong set -- it
    // asserts in a debug core and loses it silently in a release one. Refused
    // here so both builds behave alike and the caller is told.
    CHECK ( store.pushPaging() == E_PAGESTATE );
    ShowError ( L"a second PushPaging", E_PAGESTATE );

    CHECK ( SUCCEEDED ( store.popPaging() ) );
    CHECK ( store.popPaging() == E_PAGESTATE );   // and nothing to restore

    // --- unregister -----------------------------------------------------------
    CHECK ( SUCCEEDED ( store.setPagingSink ( NULL ) ) );
    pSink->Reset();
    CHECK ( SUCCEEDED ( store.pageIn ( pos, &bOk ) ) );
    CHECK ( pSink->PageIns() == 0 );              // really unhooked
    pSink->Release();

    Note ( L"A paging handler must not block and must not re-enter the store" );
    Note ( L"beyond the subtree it was asked for: it runs under the store lock." );
}


int main ( )
{
    InitConsole();

    wprintf ( L"=== MgrPersistTestCom - a Msgcore store as a COM document ===\n" );
    fflush ( stdout );

    Apartment apt;
    if ( !apt.ok() ) return SetupFailure ( L"CoInitializeEx", E_FAIL );

    Store store;
    if ( !store.ok() ) return SetupFailure ( L"CoCreateInstance(MsgcoreCom.MsgStore)", store.hr() );

    Demo_Store    ( store );
    Demo_SaveLoad ( store );
    Demo_P2Pos    ( store );
    Demo_Triggers ( store );
    Demo_Paging ( store );

    return Verdict();
}
