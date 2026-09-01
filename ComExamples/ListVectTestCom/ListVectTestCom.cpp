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
// ListVectTestCom.cpp
//
// The counterpart of ..\DirectExamples\ListVectTest -- and the
// harness where this tree stops being a translation and starts being a report.
//
// THE HEADLINE, UP FRONT: a linked list and an indexed vector CAN NOW BE
// CREATED through this face. They could not be when this harness was first
// written, and the reason was never this layer -- Msgcore_c.h, the flat C ABI
// that MsgcoreCom is built on and its only view of Msgcore, had no constructor
// for either. It wrapped a field that ALREADY held one, and every declare in
// the header made a DATA node, so nothing above it could produce the input
// those wrappers needed.
//
// The header now carries msgcore_field_declare_list / _declare_vect, so
// sections 1 and 2 no longer pin an absence: they create both container kinds,
// mutate them through the COM face, and read the mutations back out of the
// store to prove the handles are LIVE rather than private copies.
//
// Three things this turned up are worth knowing before reading further, and
// all three are Msgcore's rather than this tier's:
//
//   Field.Child CANNOT reach a list or a vect. It resolves through
//   msgcore_field_child, which is SelectItem-based, and SelectItem THROWS on a
//   container node -- a list is not an "item". ChildList / ChildVect exist for
//   exactly that gap and section 1 pins it.
//
//   A list is walked in C++ by a raw heap address, which a relocation
//   invalidates, so no such position crosses the ABI. Indexing is offered
//   instead and costs a walk from the head; For Each walks once.
//
//   An element's DECLARED TYPE survives a write. Assigning a Double into an
//   Int cell converts rather than retypes, because otherwise a sequence's type
//   would depend on the order its cells were written.
//
// Sections 3 to 5 then cover what this tier has for ordered and bulk data more
// generally -- nesting, and the cursor.
//
// Exit codes: 0 SUCCESS  1 SETUP  2 ASSERT  3 a check failed.

#include "../common/ComHarness.h"

using namespace msgc;


// =========================================================================
// 1. IMsgListCom -- created, mutated, and read back live
// =========================================================================
static void Demo_List ( Store& store )
{
    Section ( L"1. IMsgListCom -- create a list, fill it, mutate it, re-read it" );

    Field bag;
    CHECK ( SUCCEEDED ( store.root().declare ( L"bag", Var ( 0 ), &bag ) ) );

    // Ordinary data nodes are still not lists, whatever their declared type.
    CHECK ( SUCCEEDED ( bag.declare ( L"anInt", Var ( 10 ) ) ) );
    CHECK ( !bag.child ( L"anInt" ).isList() );
    CHECK ( !store.root().isList() );

    // --- create -------------------------------------------------------------
    List l;
    CHECK ( SUCCEEDED ( bag.declareList ( L"samples", l ) ) );
    CHECK ( l.ok() );
    CHECK ( l.count() == 0 );

    CHECK ( SUCCEEDED ( l.addTail ( Var ( 11 ) ) ) );
    CHECK ( SUCCEEDED ( l.addTail ( Var ( 22 ) ) ) );
    CHECK ( SUCCEEDED ( l.addTail ( Var ( L"tail" ) ) ) );
    CHECK ( SUCCEEDED ( l.addHead ( Var ( 0.5 ) ) ) );
    CHECK ( l.count() == 4 );

    // --- read by index, typed as stored --------------------------------------
    CHECK ( l.item ( 0 ).asDouble() == 0.5 );
    CHECK ( l.item ( 1 ).asLong()   == 11 );
    CHECK ( l.item ( 2 ).asLong()   == 22 );
    CHECK ( l.item ( 3 ).asText() == L"tail" );

    CHECK ( l.typeAt ( 0 ) == msgcTypeDouble );
    CHECK ( l.typeAt ( 1 ) == msgcTypeInt32 );
    CHECK ( l.typeAt ( 3 ) == msgcTypeWStr );

    // Out of range is a NAMED refusal, not an empty value.
    Var scratch;
    CHECK ( l.itemHr ( 99, scratch ) == E_RANGE );
    ShowError ( L"List.Item past the end", E_RANGE );

    // --- mutate ---------------------------------------------------------------
    CHECK ( SUCCEEDED ( l.setItem ( 1, Var ( 99 ) ) ) );
    CHECK ( l.item ( 1 ).asLong() == 99 );

    // The cell's DECLARED TYPE survives the write: a Double into an Int32 cell
    // converts, it does not retype the cell. Otherwise a list's element types
    // would depend on the order a caller happened to write them in.
    CHECK ( SUCCEEDED ( l.setItem ( 1, Var ( 7.9 ) ) ) );
    CHECK ( l.typeAt ( 1 ) == msgcTypeInt32 );

    CHECK ( l.removeAt ( 1 ) );
    CHECK ( l.count() == 3 );
    CHECK ( !l.removeAt ( 99 ) );

    // --- LIVE, not a private copy ---------------------------------------------
    // Everything above went through the handle declareList returned. Reach the
    // node again by name and the mutations are there, which is the whole claim.
    List back;
    CHECK ( SUCCEEDED ( bag.childList ( L"samples", back ) ) );
    CHECK ( back.count() == 3 );
    CHECK ( back.item ( 0 ).asDouble() == 0.5 );
    CHECK ( back.item ( 1 ).asLong()   == 22 );

    // --- Child CAN do this now, which it could not ----------------------------
    // Child used to resolve through SelectItem, which THROWS on a container
    // ("a list is not an item"), so `Child("samples")` failed for a list that was
    // plainly there and ChildList existed for exactly that gap. Child reaches it,
    // and ChildList/ChildVect are the TYPED spellings rather than the only ones.
    Field asNode = bag.child ( L"samples" );
    CHECK ( asNode.ok() );
    CHECK ( asNode.isList() );
    CHECK ( asNode.name() == L"samples" );

    // Wrong kind and absent are different answers, so neither needs a pre-check.
    Vect wrongKind;
    CHECK ( bag.childVect ( L"samples", wrongKind ) == E_NOT_VECT );
    List absent;
    CHECK ( bag.childList ( L"nosuch", absent ) == E_NO_FIELD );
    ShowError ( L"ChildVect on a node that is a list", E_NOT_VECT );

    // A data node still refuses the list view.
    List l2;
    CHECK ( bag.child ( L"anInt" ).list ( l2 ) == E_NOT_LIST );

    // --- For Each ---------------------------------------------------------------
    Ptr<IUnknown> spEnum;
    CHECK ( SUCCEEDED ( back.newEnum ( spEnum.addr() ) ) );
    int nSeen = 0;
    CHECK ( SUCCEEDED ( EnumVars ( spEnum.get(), [&] ( Var& ) { ++nSeen; } ) ) );
    CHECK ( nSeen == 3 );
    Note ( L"For Each walks a list once; indexing walks from the head every time." );

    CHECK ( SUCCEEDED ( back.truncate() ) );
    CHECK ( back.count() == 0 );
}


// =========================================================================
// 2. IMsgVectCom -- and the Count that used to be missing
// =========================================================================
//
// This interface carried a note for as long as it existed saying it had no
// Count and no _NewEnum, that the omission was the flat ABI's rather than this
// layer's, and that "the day msgcore_vect_get_count exists, Count and _NewEnum
// are appended here and the IID does not change". That day came. Both are
// exercised below and the IID is indeed unchanged, so a client built against
// the older type library still binds.
//
static void Demo_Vect ( Store& store )
{
    Section ( L"2. IMsgVectCom -- create, index, mutate, and the new Count" );

    Field bag = store.root().child ( L"bag" );
    CHECK ( bag.ok() );

    Vect v;
    CHECK ( SUCCEEDED ( bag.declareVect ( L"payload", 3, msgcTypeInt32, v ) ) );
    CHECK ( v.ok() );
    CHECK ( v.count() == 3 );                      // the count that did not exist

    CHECK ( SUCCEEDED ( v.setItem ( 0, Var ( 5 ) ) ) );
    CHECK ( SUCCEEDED ( v.setItem ( 1, Var ( 6 ) ) ) );
    CHECK ( SUCCEEDED ( v.setItem ( 2, Var ( 7 ) ) ) );
    CHECK ( v.item ( 0 ).asLong() == 5 );
    CHECK ( v.item ( 2 ).asLong() == 7 );
    CHECK ( v.typeAt ( 0 ) == msgcTypeInt32 );

    LONG scratch = 0;
    CHECK ( v.typeAtHr ( 99, &scratch ) == E_RANGE );
    CHECK ( v.setItem ( 99, Var ( 1 ) ) == E_RANGE );

    // The element prototype fixes the width, so a Double vect stores doubles.
    Vect vd;
    CHECK ( SUCCEEDED ( bag.declareVect ( L"reals", 2, msgcTypeDouble, vd ) ) );
    CHECK ( SUCCEEDED ( vd.setItem ( 0, Var ( 1.5 ) ) ) );
    CHECK ( vd.typeAt ( 0 ) == msgcTypeDouble );
    CHECK ( vd.item ( 0 ).asDouble() == 1.5 );

    // A zero-length vect is legal.
    Vect vg;
    CHECK ( SUCCEEDED ( bag.declareVect ( L"grow", 0, msgcTypeInt32, vg ) ) );
    CHECK ( vg.count() == 0 );

    // --- live, again ------------------------------------------------------------
    Vect back;
    CHECK ( SUCCEEDED ( bag.childVect ( L"payload", back ) ) );
    CHECK ( back.count() == 3 );
    CHECK ( back.item ( 1 ).asLong() == 6 );

    List wrongKind;
    CHECK ( bag.childList ( L"payload", wrongKind ) == E_NOT_LIST );

    // --- For Each over the elements ----------------------------------------------
    Ptr<IUnknown> spEnum;
    CHECK ( SUCCEEDED ( back.newEnum ( spEnum.addr() ) ) );
    int nSeen = 0;
    CHECK ( SUCCEEDED ( EnumVars ( spEnum.get(), [&] ( Var& ) { ++nSeen; } ) ) );
    CHECK ( nSeen == 3 );

    // The aAlloc[32] spill seam -- a vect keeps its first 32 element addresses
    // inline and spills the rest into continuation blocks -- IS reachable now,
    // because a vect of any size can be built. Crossing it changes nothing a
    // caller can observe, which is the point worth recording rather than
    // assuming.
    Vect big;
    CHECK ( SUCCEEDED ( bag.declareVect ( L"wide", 70, msgcTypeInt32, big ) ) );
    CHECK ( big.count() == 70 );
    CHECK ( SUCCEEDED ( big.setItem ( 0,  Var ( 1000 ) ) ) );
    CHECK ( SUCCEEDED ( big.setItem ( 31, Var ( 1031 ) ) ) );   // last inline slot
    CHECK ( SUCCEEDED ( big.setItem ( 32, Var ( 1032 ) ) ) );   // first spilled
    CHECK ( SUCCEEDED ( big.setItem ( 69, Var ( 1069 ) ) ) );
    CHECK ( big.item ( 0 ).asLong()  == 1000 );
    CHECK ( big.item ( 31 ).asLong() == 1031 );
    CHECK ( big.item ( 32 ).asLong() == 1032 );
    CHECK ( big.item ( 69 ).asLong() == 1069 );
    Note ( L"The 32-element spill seam is crossed here and is invisible above the heap." );
}


// =========================================================================
// 3. What DOES nest -- fields inside fields, arbitrarily deep
// =========================================================================
//
// ListVectTest section 4 nests containers inside containers. The containers
// available here are nodes, and they nest without limit -- which is the point
// the original makes too: there is exactly ONE node type in the model, so a
// tree of them is the general case and a list or a vect is a specialisation.
//
static void Demo_Nesting ( Store& store )
{
    Section ( L"3. Nesting -- one node type, arbitrarily deep" );

    Field root;
    CHECK ( SUCCEEDED ( store.root().declare ( L"deep", Var ( 0 ), &root ) ) );

    // Ten levels, each an ordinary node carrying a value of its own.
    // The path grammar INTRODUCES every step with its separator -- '.' for a
    // child, '@' for an attribute -- so the chain starts at ".deep", not "deep",
    // and the root itself is "". That is what makes a path reversible: FieldAt
    // takes back exactly what Path hands out.
    Field cur = root;
    std::wstring path = L".deep";
    for ( int i = 0; i < 10; ++i )
    {
        WCHAR wszName[32];
        ::swprintf_s ( wszName, L"L%d", i );
        Field next;
        CHECK ( SUCCEEDED ( cur.declare ( wszName, Var ( i * 100 ), &next ) ) );
        cur = next;
        path += L".";
        path += wszName;
    }

    CHECK ( path == L".deep.L0.L1.L2.L3.L4.L5.L6.L7.L8.L9" );
    CHECK ( cur.path() == path );
    CHECK ( cur.asLong() == 900 );

    // ... and the whole chain is reachable in one call by path, which is what
    // makes a deep tree usable from a script.
    CHECK ( store.fieldAt ( path.c_str() ).asLong() == 900 );
    CHECK ( store.fieldAt ( L".deep.L0.L1.L2" ).asLong() == 200 );

    // Every level carries a value AND children at the same time. A node is not
    // either a leaf or an interior node; it is both whenever it wants to be.
    CHECK ( store.fieldAt ( L".deep.L0" ).asLong() == 0 );
    CHECK ( store.fieldAt ( L".deep.L0" ).count() == 1 );

    // A branch, so the tree is not merely a chain.
    Field l0 = store.fieldAt ( L".deep.L0" );
    CHECK ( SUCCEEDED ( l0.declare ( L"sibling", Var ( L"beside L1" ) ) ) );
    CHECK ( l0.count() == 2 );
    CHECK ( store.fieldAt ( L".deep.L0.sibling" ).asText() == L"beside L1" );

    // Attributes hang off interior nodes as happily as off leaves -- the second
    // collection is a property of a NODE, not of a leaf.
    Attr a;
    CHECK ( SUCCEEDED ( l0.attributes ( a, true ) ) );
    CHECK ( SUCCEEDED ( a.declare ( L"depth", Var ( 0 ) ) ) );
    CHECK ( a.count() == 1 );
    CHECK ( l0.count() == 2 );                      // descendants unchanged

    Note ( L"10 levels, one branch, attributes on an interior node: %s = %d",
           path.c_str(), (int)cur.asLong() );
}


// =========================================================================
// 4. IMsgCursorCom -- generic traversal
// =========================================================================
//
// The cursor is what this tier actually has for "walk a collection you did not
// build", and it is a first-class object here rather than the C++ P3PmsgCurs's
// position-and-reference pair.
//
// ONE THING IS DELIBERATELY NOT A TRANSLATION, and it is the most important
// line in this file. The flat library's IsEoCursor is
//
//      nItems <= 0 || m_nItem >= nItems - 1
//
// -- TRUE ON THE LAST ELEMENT, not after it. So the loop everyone writes,
// `for ( c.Seek(); !c.IsEoCursor(); ++c )`, silently visits every element but
// the last, with no error and no short read: a three-child node enumerates as
// two. IMsgCursorCom.EndOfCursor means what its help string says instead --
// "the cursor has walked off the end" -- so the natural loop is correct here.
// That is a DIFFERENT predicate from the flat one, on purpose.
//
static void Demo_Cursor ( Store& store )
{
    Section ( L"4. IMsgCursorCom -- generic traversal, and the EndOfCursor trap" );

    Field bench;
    CHECK ( SUCCEEDED ( store.root().declare ( L"bench", Var ( 0 ), &bench ) ) );

    const LPCWSTR names[] = { L"alpha", L"bravo", L"charlie", L"delta", L"echo" };
    for ( int i = 0; i < 5; ++i )
        CHECK ( SUCCEEDED ( bench.declare ( names[i], Var ( ( i + 1 ) * 10 ) ) ) );

    CHECK ( bench.count() == 5 );

    Cursor c;
    CHECK ( SUCCEEDED ( bench.cursor ( c ) ) );
    CHECK ( c.count() == 5 );

    // THE LOOP. If EndOfCursor had been a transliteration of IsEoCursor, this
    // would count 4 and sum 100 -- and would look right.
    int nVisited = 0, nSum = 0;
    std::wstring order;
    for ( c.seek(); !c.eoc(); c.next() )
    {
        order += c.name();
        order += L" ";
        nSum  += c.field().asLong();
        ++nVisited;
    }
    CHECK ( nVisited == 5 );
    CHECK ( nSum == 150 );
    CHECK ( order == L"alpha bravo charlie delta echo " );
    Note ( L"forward : %d elements summing to %d", nVisited, nSum );
    Note ( L"order   : %s", order.c_str() );

    // Positioning. Index is a POSITION, never a handle -- the set is live.
    CHECK ( c.gotoIndex ( 2 ) );
    CHECK ( c.index() == 2 );
    CHECK ( c.name() == L"charlie" );
    CHECK ( c.isItem() );
    CHECK ( !c.isList() );
    CHECK ( !c.isVect() );

    CHECK ( c.gotoName ( L"echo" ) );
    CHECK ( c.index() == 4 );
    CHECK ( c.field().asLong() == 50 );

    CHECK ( !c.gotoName ( L"nobody" ) );            // False, position unchanged
    CHECK ( c.index() == 4 );
    CHECK ( !c.gotoIndex ( 99 ) );
    CHECK ( c.index() == 4 );

    // Walking off the end is a state, and Index reports it as -1 rather than
    // as a plausible position.
    CHECK ( c.next() == S_FALSE );
    CHECK ( c.eoc() );
    CHECK ( c.index() == -1 );

    CHECK ( SUCCEEDED ( c.seek() ) );
    CHECK ( c.soc() );
    CHECK ( !c.eoc() );
    CHECK ( c.index() == 0 );

    // An element reached through a cursor is handed back LIVE, so a walk can
    // also be an edit. That used to be true only over a CHILD collection: over
    // an attribute collection there was no live accessor to reach one with, and
    // IsLive was how a caller found out which of the two they had. Both are live
    // now, and IsLive is gone with the distinction.
    CHECK ( c.gotoName ( L"bravo" ) );
    Field bravo = c.field();
    CHECK ( bravo.ok() );
    CHECK ( bravo.path() == L".bench.bravo" );
    CHECK ( SUCCEEDED ( bravo.setValue ( Var ( 999 ) ) ) );
    CHECK ( bench.child ( L"bravo" ).asLong() == 999 );

    // A CURSOR SURVIVES A MUTATION, which the flat P3PmsgCurs does not: it is
    // rebuilt from the owner and re-seeked on every call, so a heap relocation
    // underneath it is invisible. Add 40 children and the position still means
    // what it meant.
    CHECK ( c.gotoIndex ( 1 ) );
    for ( int i = 0; i < 40; ++i )
    {
        WCHAR wszName[32];
        ::swprintf_s ( wszName, L"filler%02d", i );
        CHECK ( SUCCEEDED ( bench.declare ( wszName, Var ( i ) ) ) );
    }
    CHECK ( c.count() == 45 );
    CHECK ( c.index() == 1 );
    CHECK ( c.name() == L"bravo" );                 // same element, moved heap
}


// =========================================================================
// 5. _NewEnum -- For Each, and why it is a snapshot
// =========================================================================
//
// `For Each child In field` is what a scripting host compiles a walk into, and
// its contract is ONE PASS OVER A FIXED SET. A live position cannot promise
// that across a mutation -- delete an element and everything after it shifts
// down into the gap -- so _NewEnum takes the elements up front instead.
//
// The difference is not academic: deleting as you walk is the single most
// common thing a `For Each` body does.
//
static void Demo_ForEach ( Store& store )
{
    Section ( L"5. _NewEnum -- a snapshot, so a handler may delete as it walks" );

    Field bin;
    CHECK ( SUCCEEDED ( store.root().declare ( L"inbox", Var ( 0 ), &bin ) ) );

    for ( int i = 0; i < 8; ++i )
    {
        WCHAR wszName[32];
        ::swprintf_s ( wszName, L"msg%d", i );
        CHECK ( SUCCEEDED ( bin.declare ( wszName, Var ( i ) ) ) );
    }
    CHECK ( bin.count() == 8 );

    // A plain read pass sees every element, in declaration order.
    int nSeen = 0, nSum = 0;
    CHECK ( SUCCEEDED ( bin.forEach ( [&] ( Field& f ) {
        ++nSeen;
        nSum += f.asLong();
    } ) ) );
    CHECK ( nSeen == 8 );
    CHECK ( nSum == 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7 );

    // The elements are LIVE, so For Each is also an edit pass.
    CHECK ( SUCCEEDED ( bin.forEach ( [&] ( Field& f ) {
        f.setValue ( Var ( f.asLong() * 2 ) );
    } ) ) );
    CHECK ( bin.child ( L"msg7" ).asLong() == 14 );

    // AND THE ONE THAT MATTERS: delete the even ones while walking. Against a
    // live position this either skips elements or walks off the end; against a
    // snapshot it does exactly what it reads as.
    int nVisitedDuringDelete = 0;
    CHECK ( SUCCEEDED ( bin.forEach ( [&] ( Field& f ) {
        ++nVisitedDuringDelete;
        const LONG v = f.asLong();
        if ( ( v / 2 ) % 2 == 0 )                   // the ones that were even
            store.root().child ( L"inbox" ).remove ( f.name().c_str() );
    } ) ) );
    CHECK ( nVisitedDuringDelete == 8 );            // every element, despite the deletes
    CHECK ( bin.count() == 4 );
    CHECK ( !bin.exists ( L"msg0" ) );
    CHECK ( bin.exists ( L"msg1" ) );
    CHECK ( !bin.exists ( L"msg2" ) );
    CHECK ( bin.exists ( L"msg7" ) );

    Note ( L"visited %d, %d survived: %s", nVisitedDuringDelete, bin.count(),
           L"msg1 msg3 msg5 msg7" );

    // Cursor.Delete is the other way to do it, and is a LIVE position: it
    // removes the current element and the index does NOT advance, because
    // everything after it has shifted down into that slot.
    Cursor c;
    CHECK ( SUCCEEDED ( bin.cursor ( c ) ) );
    CHECK ( SUCCEEDED ( c.seek() ) );
    CHECK ( c.count() == 4 );
    const std::wstring first = c.name();
    CHECK ( SUCCEEDED ( c.remove() ) );
    CHECK ( c.count() == 3 );
    CHECK ( !bin.exists ( first.c_str() ) );
    CHECK ( c.index() == 0 );                       // still 0: the set moved, not us

    // Walk what is left, then an empty collection -- the degenerate case the
    // count-based loop has to get right too.
    int nLeft = 0;
    for ( c.seek(); !c.eoc(); c.next() ) ++nLeft;
    CHECK ( nLeft == 3 );

    CHECK ( SUCCEEDED ( bin.truncate() ) );
    CHECK ( bin.count() == 0 );
    Cursor empty;
    CHECK ( SUCCEEDED ( bin.cursor ( empty ) ) );
    CHECK ( empty.count() == 0 );
    CHECK ( empty.eoc() );                          // immediately, with no element read

    int nNever = 0;
    for ( empty.seek(); !empty.eoc(); empty.next() ) ++nNever;
    CHECK ( nNever == 0 );

    int nNeverEnum = 0;
    CHECK ( SUCCEEDED ( bin.forEach ( [&] ( Field& ) { ++nNeverEnum; } ) ) );
    CHECK ( nNeverEnum == 0 );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    InitConsole();

    wprintf ( L"=== ListVectTestCom - ordered data through COM, and where it stops ===\n" );
    fflush ( stdout );

    Apartment apt;
    if ( !apt.ok() ) return SetupFailure ( L"CoInitializeEx", E_FAIL );

    Store store;
    if ( !store.ok() ) return SetupFailure ( L"CoCreateInstance(MsgcoreCom.MsgStore)", store.hr() );

    Demo_List ( store );
    Demo_Vect ( store );
    Demo_Nesting      ( store );
    Demo_Cursor       ( store );
    Demo_ForEach      ( store );

    return Verdict();
}
