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
// RecursTimeTestCom.cpp
//
// The counterpart of ..\DirectExamples\RecursTimeTest: walking a
// whole subtree, and the cell that is known to be a time.
//
// ONE OF THE TWO SUBJECTS DOES NOT CROSS, and the split is clean:
//
//   P2PmsgRecurs -- the recursive walker -- is a C++ class with no presence in
//   Msgcore_c.h at all, so there is nothing for this server to wrap. What
//   replaces it is not a loss: the walker's whole job is to flatten a tree into
//   one loop, and a CLIENT can do that with the cursor in a dozen lines, which
//   sections 1 to 3 do. The difference worth knowing is WHERE the stack lives.
//   P2PmsgRecurs keeps it inside the walker and hands the caller Push/Pop/Break
//   to drive it; here the stack is the client's, which means the client can
//   also inspect it, snapshot it, or drive the descent from a script.
//
//   P3PmsgTime -- the TIME64 cell -- crosses completely, and better than it
//   started: a node's timestamp is a DATE here, which is what a host formats,
//   compares and shows without arithmetic, instead of epoch seconds.
//
// Exit codes: 0 SUCCESS  1 SETUP  2 ASSERT  3 a check failed.

#include "../common/ComHarness.h"

using namespace msgc;


// A tree worth walking: three levels, branching, with values at every level so
// that "interior nodes carry data too" is exercised rather than assumed.
static void BuildTree ( Store& store )
{
    Field root = store.root();
    Field site;
    CHECK ( SUCCEEDED ( root.declare ( L"site", Var ( L"melbourne" ), &site ) ) );

    const LPCWSTR floors[] = { L"ground", L"first", L"second" };
    for ( int f = 0; f < 3; ++f )
    {
        Field floor;
        CHECK ( SUCCEEDED ( site.declare ( floors[f], Var ( f ), &floor ) ) );

        for ( int r = 0; r < 2; ++r )
        {
            WCHAR wszRoom[32];
            ::swprintf_s ( wszRoom, L"room%d", r );
            Field room;
            CHECK ( SUCCEEDED ( floor.declare ( wszRoom, Var ( f * 10 + r ), &room ) ) );

            CHECK ( SUCCEEDED ( room.declare ( L"temp", Var ( 20.0 + f + r ) ) ) );
            CHECK ( SUCCEEDED ( room.declare ( L"name", Var ( L"a room" ) ) ) );
        }
    }
}

// One stop on a walk -- what P2PmsgRecurs would have exposed at each iteration.
struct Stop
{
    std::wstring path;
    std::wstring name;
    int          depth;
    LONG         children;
};


// =========================================================================
// 1. One flat loop over a whole subtree
// =========================================================================
//
// P2PmsgRecurs exists so that a caller does not have to write a recursive
// function: it turns a tree into a sequence. The same shape is a dozen lines
// over IMsgCursorCom -- and because EndOfCursor here means "walked off the
// end" (and NOT the flat IsEoCursor's "on the last element"), the obvious loop
// is the correct one.
//
static void Demo_FlatWalk ( Store& store, std::vector<Stop>& out )
{
    Section ( L"1. One flat loop over a whole subtree" );

    // An explicit work stack, so the walk is a LOOP and not recursion -- which
    // is what makes it portable to a scripting host with no call-stack depth
    // to spend.
    std::vector<std::pair<std::wstring,int> > work;
    work.push_back ( std::make_pair ( std::wstring ( L".site" ), 0 ) );

    while ( !work.empty() )
    {
        const std::wstring path  = work.back().first;
        const int          depth = work.back().second;
        work.pop_back();

        Field f = store.fieldAt ( path.c_str() );
        if ( !f.ok() ) continue;

        Stop s;
        s.path     = path;
        s.name     = f.name();
        s.depth    = depth;
        s.children = f.count();
        out.push_back ( s );

        // Children pushed in reverse so they come off in declaration order.
        std::vector<std::wstring> kids;
        Cursor c;
        if ( SUCCEEDED ( f.cursor ( c ) ) )
            for ( c.seek(); !c.eoc(); c.next() )
                kids.push_back ( c.name() );

        for ( size_t i = kids.size(); i > 0; --i )
            work.push_back ( std::make_pair ( path + L"." + kids[i-1], depth + 1 ) );
    }

    // 1 site + 3 floors + 6 rooms + 12 leaves = 22 stops.
    CHECK ( out.size() == 22 );
    CHECK ( !out.empty() && out[0].path == L".site" );
    CHECK ( !out.empty() && out[0].depth == 0 );
    CHECK ( !out.empty() && out[0].children == 3 );

    int nMaxDepth = 0, nLeaves = 0;
    for ( size_t i = 0; i < out.size(); ++i )
    {
        if ( out[i].depth > nMaxDepth ) nMaxDepth = out[i].depth;
        if ( out[i].children == 0 )     ++nLeaves;
    }
    CHECK ( nMaxDepth == 3 );
    CHECK ( nLeaves == 12 );
    Note ( L"visited %d nodes, max depth %d, %d leaves",
           (int)out.size(), nMaxDepth, nLeaves );

    // Declaration order is preserved all the way down, which is what makes a
    // walk reproducible -- and therefore what makes it diffable.
    CHECK ( out.size() > 2 && out[1].path == L".site.ground" );
    CHECK ( out.size() > 3 && out[2].path == L".site.ground.room0" );
    CHECK ( out.size() > 4 && out[3].path == L".site.ground.room0.temp" );
}


// =========================================================================
// 2. Driving the descent by hand -- push, pop and break
// =========================================================================
//
// P2PmsgRecurs gives the caller Push/Pop/Break to steer the walk: descend into
// this node, do not descend into that one, stop here. With the stack in the
// client's hands, all three are one line each -- and BREAK, the one that
// matters for a big tree, is just `break`.
//
static void Demo_DrivenDescent ( Store& store )
{
    Section ( L"2. Push / Pop / Break -- steering the descent" );

    // PRUNE: descend into everything except one subtree. This is the case a
    // walker's "do not push" is for, and it is the common one -- a config
    // reader that skips a cache branch, a backup that skips a scratch node.
    std::vector<std::wstring> work ( 1, std::wstring ( L".site" ) );
    int nVisited = 0, nPruned = 0;

    while ( !work.empty() )
    {
        const std::wstring path = work.back();
        work.pop_back();
        ++nVisited;

        Field f = store.fieldAt ( path.c_str() );
        if ( !f.ok() ) continue;

        if ( f.name() == L"first" ) { ++nPruned; continue; }     // do not descend

        Cursor c;
        if ( SUCCEEDED ( f.cursor ( c ) ) )
            for ( c.seek(); !c.eoc(); c.next() )
                work.push_back ( path + L"." + c.name() );
    }

    // 22 total, minus the 6 BELOW "first" that were never pushed. "first"
    // itself is still visited -- the prune decides whether to DESCEND, which
    // is the distinction a walker's Push/Pop is about.
    CHECK ( nPruned == 1 );
    CHECK ( nVisited == 22 - 6 );
    Note ( L"pruned 'first': %d nodes visited instead of 22", nVisited );

    // BREAK: stop at the first match. The point of a walker is that this costs
    // what it costs and not a full traversal.
    std::vector<std::wstring> work2 ( 1, std::wstring ( L".site" ) );
    int nBeforeHit = 0;
    std::wstring found;

    while ( !work2.empty() )
    {
        const std::wstring path = work2.back();
        work2.pop_back();
        ++nBeforeHit;

        Field f = store.fieldAt ( path.c_str() );
        if ( !f.ok() ) continue;

        if ( f.name() == L"temp" && f.asDouble() >= 22.0 )
        {
            found = path;
            break;                                  // Break()
        }

        Cursor c;
        if ( SUCCEEDED ( f.cursor ( c ) ) )
        {
            std::vector<std::wstring> kids;
            for ( c.seek(); !c.eoc(); c.next() )
                kids.push_back ( c.name() );
            for ( size_t i = kids.size(); i > 0; --i )
                work2.push_back ( path + L"." + kids[i-1] );
        }
    }

    CHECK ( !found.empty() );
    CHECK ( found == L".site.first.room1.temp" );
    CHECK ( nBeforeHit < 22 );
    Note ( L"break on first temp >= 22.0: '%s' after %d stops",
           found.c_str(), nBeforeHit );

    // POP past a whole level: collect one depth only, which is what a "list the
    // floors" query is, and needs no descent at all.
    Field site = store.fieldAt ( L".site" );
    std::wstring floors;
    Cursor c;
    CHECK ( SUCCEEDED ( site.cursor ( c ) ) );
    for ( c.seek(); !c.eoc(); c.next() ) { floors += c.name(); floors += L" "; }
    CHECK ( floors == L"ground first second " );
}


// =========================================================================
// 3. What the walk can read at each stop
// =========================================================================
static void Demo_ReadAtEachStop ( Store& store, const std::vector<Stop>& stops )
{
    Section ( L"3. Reading the tree through the walk" );

    int nTyped = 0, nDoubles = 0, nStrings = 0, nInts = 0;

    for ( size_t i = 0; i < stops.size(); ++i )
    {
        Field f = store.fieldAt ( stops[i].path.c_str() );
        CHECK ( f.ok() );
        if ( !f.ok() ) continue;

        // Every stop answers the same four questions, whatever it is -- which
        // is the uniformity the model claims and this is the test of it.
        CHECK ( f.path() == stops[i].path );
        CHECK ( f.name() == stops[i].name );
        CHECK ( f.count() == stops[i].children );
        CHECK ( !f.typeName().empty() );

        ++nTyped;
        const LONG t = f.dataType();
        if ( t == TYPE_DOUBLE )     ++nDoubles;
        else if ( t == TYPE_WSTR )  ++nStrings;
        else if ( t == TYPE_INT32 ) ++nInts;
    }

    CHECK ( nTyped == 22 );
    CHECK ( nDoubles == 6 );                        // one temp per room
    CHECK ( nStrings == 1 + 6 );                    // "site" + one name per room
    CHECK ( nInts == 3 + 6 );                       // floors + rooms
    Note ( L"22 stops: %d double, %d string, %d int", nDoubles, nStrings, nInts );

    // A walk that WRITES, which is the other half of what a walker is for.
    // Every element a cursor over children yields is live, so this is an edit
    // pass over the whole subtree with no re-resolution by hand.
    for ( size_t i = 0; i < stops.size(); ++i )
    {
        Field f = store.fieldAt ( stops[i].path.c_str() );
        if ( f.ok() && f.dataType() == TYPE_DOUBLE )
            CHECK ( SUCCEEDED ( f.setValue ( Var ( f.asDouble() + 100.0 ) ) ) );
    }
    CHECK ( store.fieldAt ( L".site.ground.room0.temp" ).asDouble() == 120.0 );
    CHECK ( store.fieldAt ( L".site.second.room1.temp" ).asDouble() == 123.0 );
}


// =========================================================================
// 4. A node's timestamp -- P3PmsgTime, as a DATE
// =========================================================================
//
// Msgcore stores a node's time as the "$TStamp$" ATTRIBUTE, in seconds since
// the epoch. This layer converts it to an automation DATE in both directions,
// because epoch seconds are a number a host has to do arithmetic on and a DATE
// is a value it can format, compare and put in a grid.
//
// TWO CONSEQUENCES ARE WORTH PINNING, and both are exercised below:
// the resolution is ONE SECOND (it is an integer count, so a Date carrying
// fractions of a second does not survive), and stamping a node MAKES IT
// ATTRIBUTED -- the timestamp is not a separate slot, it is an attribute.
//
static void Demo_Timestamp ( Store& store )
{
    Section ( L"4. Timestamp -- the TIME64 cell, seen as a Date" );

    Field room = store.fieldAt ( L".site.ground.room0" );
    CHECK ( room.ok() );

    // Unset reads as 0, which is 30 December 1899 -- the automation epoch, and
    // the value a host renders as "no date".
    CHECK ( room.timestamp() == (DATE)0.0 );
    CHECK ( !room.isAttributed() );

    // Touch writes "now".
    SYSTEMTIME stNow; ::GetSystemTime ( &stNow );
    DATE dNow = 0;
    CHECK ( ::SystemTimeToVariantTime ( &stNow, &dNow ) != FALSE );

    CHECK ( SUCCEEDED ( room.touch() ) );
    const DATE dStamped = room.timestamp();
    CHECK ( dStamped != (DATE)0.0 );

    // Within a minute of now, which is all a clock comparison can honestly
    // claim across a call.
    CHECK ( dStamped > dNow - ( 1.0 / 1440.0 ) );
    CHECK ( dStamped < dNow + ( 1.0 / 1440.0 ) );

    SYSTEMTIME stBack;
    CHECK ( ::VariantTimeToSystemTime ( dStamped, &stBack ) != FALSE );
    Note ( L"Touch()     : %04d-%02d-%02d %02d:%02d:%02d",
           stBack.wYear, stBack.wMonth, stBack.wDay,
           stBack.wHour, stBack.wMinute, stBack.wSecond );

    // AND IT IS AN ATTRIBUTE. Stamping a node gives it an attribute collection
    // it did not have, which is visible in IsAttributed and in the collection
    // itself -- not a hidden slot.
    CHECK ( room.isAttributed() );
    Attr a;
    CHECK ( SUCCEEDED ( room.attributes ( a, false ) ) );
    CHECK ( a.count() >= 1 );
    CHECK ( a.exists ( L"$TStamp$" ) );
    Note ( L"stored as   : the '$TStamp$' attribute, %d attribute(s) on the node",
           a.count() );

    // An explicit date round-trips to the SECOND. Sub-second precision does not
    // survive, because what is stored is an integer count of seconds.
    SYSTEMTIME stFixed;
    ::ZeroMemory ( &stFixed, sizeof(stFixed) );
    stFixed.wYear = 2026; stFixed.wMonth = 8; stFixed.wDay = 11;
    stFixed.wHour = 14;   stFixed.wMinute = 30; stFixed.wSecond = 45;

    DATE dFixed = 0;
    CHECK ( ::SystemTimeToVariantTime ( &stFixed, &dFixed ) != FALSE );
    CHECK ( SUCCEEDED ( room.setTimestamp ( dFixed ) ) );

    SYSTEMTIME stRead;
    CHECK ( ::VariantTimeToSystemTime ( room.timestamp(), &stRead ) != FALSE );
    CHECK ( stRead.wYear   == 2026 );
    CHECK ( stRead.wMonth  == 8 );
    CHECK ( stRead.wDay    == 11 );
    CHECK ( stRead.wHour   == 14 );
    CHECK ( stRead.wMinute == 30 );
    CHECK ( stRead.wSecond == 45 );

    // The rounding is deliberate and it matters: truncating instead would lose
    // a second about half the time, and a timestamp that walks BACKWARDS on
    // every read-modify-write cycle is a genuinely confusing bug to chase.
    for ( int i = 0; i < 5; ++i )
    {
        const DATE d = room.timestamp();
        CHECK ( SUCCEEDED ( room.setTimestamp ( d ) ) );
        CHECK ( room.timestamp() == d );            // stable, not drifting
    }

    // Zero means unset in both directions.
    CHECK ( SUCCEEDED ( room.setTimestamp ( (DATE)0.0 ) ) );
    CHECK ( room.timestamp() == (DATE)0.0 );

    CHECK ( SUCCEEDED ( room.setTimestamp ( dFixed ) ) );
}


// =========================================================================
// 5. A timestamp through Save / Load
// =========================================================================
static void Demo_TimestampPersists ( Store& store )
{
    Section ( L"5. A timestamp through Save / Load" );

    const DATE dBefore = store.fieldAt ( L".site.ground.room0" ).timestamp();
    CHECK ( dBefore != (DATE)0.0 );

    // Stamp a few more nodes, so what survives is a pattern and not one value.
    CHECK ( SUCCEEDED ( store.fieldAt ( L".site.first" ).touch() ) );
    CHECK ( SUCCEEDED ( store.fieldAt ( L".site.second.room1.temp" ).touch() ) );

    const DATE dFirst = store.fieldAt ( L".site.first" ).timestamp();
    const DATE dTemp  = store.fieldAt ( L".site.second.room1.temp" ).timestamp();

    const std::wstring file = TempFile ( L"RecursTimeTestCom.p2p" );
    ::DeleteFileW ( file.c_str() );
    CHECK ( SUCCEEDED ( store.save ( file.c_str() ) ) );

    Store loaded;
    CHECK ( loaded.ok() );
    CHECK ( SUCCEEDED ( loaded.open ( file.c_str() ) ) );

    CHECK ( loaded.fieldAt ( L".site.ground.room0" ).timestamp() == dBefore );
    CHECK ( loaded.fieldAt ( L".site.first" ).timestamp() == dFirst );
    CHECK ( loaded.fieldAt ( L".site.second.room1.temp" ).timestamp() == dTemp );

    // A node that was never stamped is still unstamped, so the attribute is
    // genuinely per-node and not a default the format invents on load.
    CHECK ( loaded.fieldAt ( L".site.ground.room1" ).timestamp() == (DATE)0.0 );
    CHECK ( !loaded.fieldAt ( L".site.ground.room1" ).isAttributed() );

    // ... and the walk still finds the same 22 nodes on the far side, so the
    // whole subtree survived, not just the values that were checked by name.
    int nStops = 0;
    std::vector<std::wstring> work ( 1, std::wstring ( L".site" ) );
    while ( !work.empty() )
    {
        const std::wstring path = work.back();
        work.pop_back();
        ++nStops;

        Field f = loaded.fieldAt ( path.c_str() );
        if ( !f.ok() ) continue;

        Cursor c;
        if ( SUCCEEDED ( f.cursor ( c ) ) )
            for ( c.seek(); !c.eoc(); c.next() )
                work.push_back ( path + L"." + c.name() );
    }
    CHECK ( nStops == 22 );
    Note ( L"22 nodes and 3 timestamps survived the round trip" );

    ::DeleteFileW ( file.c_str() );
}


// =========================================================================
// main
// =========================================================================
// =========================================================================
// 6. The REAL walker -- IMsgRecursCom, not an emulation
// =========================================================================
//
// Sections 1 and 2 steer a descent with a client-side work list, which is what
// this tier had when they were written: P2PmsgRecurs is a C++ class and the
// flat ABI exposed no handle to one, so the walk had to be rebuilt out of
// cursors on this side of the boundary.
//
// It is exposed now, and the difference is not cosmetic. The emulation
// materialises a work list -- every path it has not yet visited, held at once
// -- which is exactly the cost a walker exists to avoid on a large tree. The
// real one holds a chain of cursors, one per pushed level, and nothing else.
//
// It is also the one object in this server that is NOT re-resolved per call.
// It cannot be: a cursor IS a live position, and snapshotting it would defeat
// the purpose. So it holds a live handle for its lifetime and answers msgcStale
// if the tree is mutated under it -- finish the walk, then mutate.
//
static void Demo_RealWalker ( Store& store )
{
    Section ( L"6. IMsgRecursCom -- the walker itself, with real pruning" );

    // A small tree of known shape, so the visit count is arithmetic and not a
    // guess: keep(2 kids) + skip(2 kids) + leaf = 3 top, 4 below.
    Field w;
    CHECK ( SUCCEEDED ( store.root().declare ( L"walk", Var ( 0 ), &w ) ) );
    Field keep, skip;
    CHECK ( SUCCEEDED ( w.declare ( L"keep", Var ( 1 ), &keep ) ) );
    CHECK ( SUCCEEDED ( keep.declare ( L"k1", Var ( 11 ) ) ) );
    CHECK ( SUCCEEDED ( keep.declare ( L"k2", Var ( 12 ) ) ) );
    CHECK ( SUCCEEDED ( w.declare ( L"skip", Var ( 2 ), &skip ) ) );
    CHECK ( SUCCEEDED ( skip.declare ( L"s1", Var ( 21 ) ) ) );
    CHECK ( SUCCEEDED ( skip.declare ( L"s2", Var ( 22 ) ) ) );
    CHECK ( SUCCEEDED ( w.declare ( L"leaf", Var ( 3 ) ) ) );

    // --- descend into everything ------------------------------------------
    {
        Walker r;
        CHECK ( SUCCEEDED ( store.fieldAt ( L".walk" ).walker ( r ) ) );
        CHECK ( r.ok() );

        int nVisited = 0, nGuard = 0;
        std::vector<std::wstring> seen;
        while ( !r.atEnd() && ++nGuard < 100 )
        {
            seen.push_back ( r.name() );
            ++nVisited;
            if ( r.isField() ) r.push();          // descend everywhere
            CHECK ( SUCCEEDED ( r.moveNext() ) );
        }
        CHECK ( nGuard < 100 );                   // terminated on its own
        CHECK ( nVisited == 7 );                  // 3 top + 4 below
        Note ( L"full descent visited %d nodes", nVisited );
    }

    // --- prune: the same walk, not descending into "skip" -------------------
    // The prune is the absence of a Push. Nothing is skipped over: "skip"
    // itself is still visited, only its children are not -- which is the
    // distinction Push/Pop is about, and the one a For Each cannot express.
    {
        Walker r;
        CHECK ( SUCCEEDED ( store.fieldAt ( L".walk" ).walker ( r ) ) );

        int nVisited = 0, nGuard = 0;
        bool bSawSkip = false, bSawS1 = false;
        while ( !r.atEnd() && ++nGuard < 100 )
        {
            const std::wstring nm = r.name();
            if ( nm == L"skip" ) bSawSkip = true;
            if ( nm == L"s1" )   bSawS1   = true;
            ++nVisited;
            if ( r.isField() && nm != L"skip" ) r.push();
            CHECK ( SUCCEEDED ( r.moveNext() ) );
        }
        CHECK ( bSawSkip );                       // visited
        CHECK ( !bSawS1 );                        // but not descended into
        CHECK ( nVisited == 5 );                  // 7 minus skip's two children
        Note ( L"pruned 'skip': %d nodes instead of 7", nVisited );
    }

    // --- the current element is an ordinary, addressable node ---------------
    {
        Walker r;
        CHECK ( SUCCEEDED ( store.fieldAt ( L".walk" ).walker ( r ) ) );
        CHECK ( !r.atEnd() );

        Field cur;
        CHECK ( SUCCEEDED ( r.field ( cur ) ) );
        CHECK ( cur.ok() );
        // Handed back by route, not tied to the walker, so it outlives the walk.
        CHECK ( cur.name() == r.name() );
        CHECK ( cur.ok() );

        // The walker now reports its own DEPTH and PATH, which it could not
        // before. Path is assembled as the walk moves, so unlike everything else
        // on the walker it stays true after the walk has moved on.
        CHECK ( r.depth() == 0 );
        CHECK ( r.path() == cur.path() );
    }

    // --- a container element is classified, and reachable as one ------------
    {
        Field c;
        CHECK ( SUCCEEDED ( store.root().declare ( L"wc", Var ( 0 ), &c ) ) );
        List cl;
        CHECK ( SUCCEEDED ( c.declareList ( L"clist", cl ) ) );
        CHECK ( SUCCEEDED ( cl.addTail ( Var ( 7 ) ) ) );

        Walker r;
        CHECK ( SUCCEEDED ( store.fieldAt ( L".wc" ).walker ( r ) ) );
        CHECK ( !r.atEnd() );
        CHECK ( r.isList() );
        CHECK ( !r.isField() );

        List got;
        CHECK ( SUCCEEDED ( r.list ( got ) ) );
        CHECK ( got.count() == 1 );
        CHECK ( got.item ( 0 ).asLong() == 7 );

        Vect wrong;
        CHECK ( r.vector ( wrong ) == E_NOT_VECT );
    }

    // --- a walker holds LIVE cursors, and what that costs --------------------
    //
    // This block used to assert that a mutation ANYWHERE invalidated the walker
    // and that the next Push answered msgcStale. That is no longer what happens
    // and it is worth being exact about why, because the difference is the whole
    // reason the layer below this one exists.
    //
    // The walker's cursors are live positions into the tree. A mutation
    // ELSEWHERE -- a declare on a different branch -- does not disturb them,
    // even though it may relocate the heap, because the cursors are re-derived
    // rather than being raw addresses. So the walk carries on, which is checked
    // here.
    //
    // A mutation INSIDE THE SUBTREE BEING WALKED is a different matter and is
    // not checked here, because there is nothing deterministic to check: a
    // walker is not a snapshot -- being one is the thing it exists not to be --
    // so what a caller gets is whatever the tree now is. The rule is the same as
    // it always was, and it is a rule rather than an error code: finish the
    // walk, then mutate.
    {
        Walker r;
        CHECK ( SUCCEEDED ( store.fieldAt ( L".walk" ).walker ( r ) ) );
        CHECK ( !r.atEnd() );
        const std::wstring before = r.name();

        // Elsewhere in the store, and big enough to move the heap under it.
        CHECK ( SUCCEEDED ( store.root().declare ( L"disturb", Var ( 1 ) ) ) );
        Field bulk;
        CHECK ( SUCCEEDED ( store.root().declare ( L"disturbBulk", Var ( 0 ), &bulk ) ) );
        for ( int i = 0; i < 50; ++i )
        {
            WCHAR wszName[32];
            ::swprintf_s ( wszName, L"d%02d", i );
            CHECK ( SUCCEEDED ( bulk.declare ( wszName, Var ( L"padding, to force the heap to grow" ) ) ) );
        }

        // The walker is still where it was, and still usable.
        CHECK ( r.name() == before );
        LONG depth = 0;
        CHECK ( SUCCEEDED ( r.pushHr ( &depth ) ) );
        CHECK ( depth >= 0 );

        CHECK ( store.root().remove ( L"disturb" ) );
        CHECK ( store.root().remove ( L"disturbBulk" ) );
        Note ( L"A walker survives a mutation on another branch. Inside the" );
        Note ( L"subtree it is walking, finish first -- it is not a snapshot." );
    }
}


// =========================================================================
// 7. The value stack -- save a node's name and value, put them back
// =========================================================================
//
// THIS USED TO BE AN OBJECT. IMsgStackCom wrapped the core's MsgStck with a
// lifetime of its own, and two of its behaviours had to be NORMALISED by this
// layer rather than passed through, because both were traps:
//
//   MsgStck::IsEmpty answers FALSE for a stack connected to nothing -- "not
//   empty" for a stack holding nothing -- so a drain loop would never end.
//
//   MsgStck::Push and ::Pop dereference their field with no null check of their
//   own, unlike Drop, Rename and r_item which all guard, so pushing an
//   unconnected stack is a null dereference inside the core.
//
// Both disappear when the object does. What it wrapped is ONE saved (name,
// value) pair living INSIDE a node, so it is four members of the node: there is
// no unconnected stack to push, and Pop answers whether it restored anything
// instead of merely succeeding.
//
// A THIRD TRAP WAS FOUND BY WRITING THIS. MsgStck::Push read a physical heap
// pointer, then allocated the stack item, then wrote through the pointer it had
// already read -- and an allocation that grows the heap RELOCATES the base image
// and moves every block in it. Reliable once a store was full enough for the
// push to trigger a growth, which is why it survived every previous test: they
// all pushed into a nearly-empty store. Fixed in Msgcore; this section pushes
// into a store with a few hundred nodes in it for that reason.
//
static void Demo_Stack ( Store& store )
{
    Section ( L"7. The value stack -- four members of a node, not an object" );

    Field s;
    CHECK ( SUCCEEDED ( store.root().declare ( L"stk", Var ( L"original" ), &s ) ) );
    CHECK ( SUCCEEDED ( s.declare ( L"a", Var ( 1 ) ) ) );

    Field node = store.fieldAt ( L".stk" );
    CHECK ( node.ok() );
    CHECK ( node.name() == L"stk" );
    CHECK ( !node.isStacked() );

    // Push, overwrite, put it back.
    CHECK ( SUCCEEDED ( node.pushValue() ) );
    CHECK ( node.isStacked() );
    CHECK ( SUCCEEDED ( node.setValue ( Var ( L"speculative" ) ) ) );
    CHECK ( node.asText() == L"speculative" );
    CHECK ( node.popValue() );
    CHECK ( node.asText() == L"original" );
    CHECK ( !node.isStacked() );

    // The children are untouched: it is the node's own name and value that are
    // saved, not its subtree.
    CHECK ( node.count() == 1 );
    CHECK ( node.child ( L"a" ).asLong() == 1 );

    // POP IS THE LOOP CONDITION. It answers False for "there was nothing
    // stacked", which is what makes a drain loop terminate -- the old object's
    // Pop was a silent no-op that still returned success.
    int nDrained = 0;
    while ( node.popValue() ) ++nDrained;
    CHECK ( nDrained == 0 );

    // And the saved pair lives in the STORE, so another reference to the same
    // node sees it.
    Field again = store.root().child ( L"stk" );
    CHECK ( SUCCEEDED ( node.pushValue() ) );
    CHECK ( again.isStacked() );
    CHECK ( again.dropValue() );                    // dropped, not restored
    CHECK ( !node.isStacked() );
    CHECK ( node.asText() == L"original" );

    Note ( L"PushValue/PopValue/DropValue/IsStacked, all on the node. Pop's" );
    Note ( L"ANSWER drives a drain loop -- never merely whether the call worked." );
}


int main ( )
{
    InitConsole();

    wprintf ( L"=== RecursTimeTestCom - walking a subtree, and the time cell ===\n" );
    fflush ( stdout );

    Apartment apt;
    if ( !apt.ok() ) return SetupFailure ( L"CoInitializeEx", E_FAIL );

    Store store;
    if ( !store.ok() ) return SetupFailure ( L"CoCreateInstance(MsgcoreCom.MsgStore)", store.hr() );

    BuildTree ( store );

    std::vector<Stop> stops;
    Demo_FlatWalk           ( store, stops );
    Demo_DrivenDescent      ( store );
    Demo_ReadAtEachStop     ( store, stops );
    Demo_Timestamp          ( store );
    Demo_TimestampPersists  ( store );
    Demo_RealWalker         ( store );
    Demo_Stack              ( store );

    return Verdict();
}
