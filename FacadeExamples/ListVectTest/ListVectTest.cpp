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
// ListVectTest.cpp  (Light -- MsgFacade)
//
// The Msgcore CONTAINERS -- a list, a vect, and the cursor that walks a
// collection without knowing what is in it -- through MsgFacade. Same subject
// and same five sections as ..\DirectExamples\ListVectTest.
//
//   original                                 light (this file)
//   ---------------------------------------------------------------------
//   P3PmsgList oList;                        List l = node.declareList(name)
//   oList.AddListTail(P3PmsgData((int)10))   l.addInt(10)
//   oList += P3PmsgData((int)40)             l.addInt(40)
//   VBLaddr aPos = oList.GetHeadPos();       for (unsigned i = 0; i < l.count(); ++i)
//     while (aPos) oList.GetNext(aPos)         l.intAt(i)
//   P3PmsgVect oVect(3, L"R", proto)         Vect v = node.declareVect(L"R",3,type)
//   oVect.r_data(0).c_int(100            v.setIntAt(0, 100)
//   oCurs.Goto(i) / IsList / IsVect / IsItem c.next() / c.kind()
//
// TWO THINGS THE FACADE DOES NOT CARRY OVER, both deliberate and both flagged
// again where they come up below:
//
//   * NO POSITIONAL LIST WALK. The original walked a list by VBLaddr, the
//     opaque heap address of a cell -- which is exactly the thing a heap
//     relocation invalidates, and the reason this facade exists. Indexing
//     replaces it, and indexing is O(index) because the core still walks the
//     chain, so a full scan by index is O(n^2). The header says so.
//   * NO InsertAt ON A VECT. A vect is declared with its length and grown by
//     re-declaring, not by inserting in the middle. Section 3 pins the 32-slot
//     seam through the two verbs that DO exist.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 1 = SETUP, 3 = at least one check failed.

#include "MsgFacadeFn.hpp"
#include "LightHarness.h"

using namespace msgf;


// =========================================================================
// 1. A list -- an ordered sequence of value cells
// =========================================================================
//
// A list holds VALUES, not named nodes: its cells have a type and a value and
// no name of their own. That is what distinguishes it from a node's child
// collection, and why everything here is indexed rather than named.
//
static void Demo_List ( Store& st )
{
    light::Section ( L"1. a list -- an ordered sequence of value cells" );

    Node root = st.root ( );
    List l    = root.declareList ( L"Numbers" );
    CHECK ( l.count ( ) == 0 );

    l.addInt ( 10 );                         // MSGF_ADD_TAIL is the default
    l.addInt ( 20 );
    l.addInt ( 30 );
    l.addInt ( 40 );
    CHECK ( l.count ( ) == 4 );

    // The forward walk. The original's loop was over an opaque heap ADDRESS;
    // this one is over an index, which is the only form that can survive the
    // store moving underneath it.
    long long iSum = 0;
    for ( unsigned int i = 0; i < l.count ( ); ++i )
      iSum += l.intAt ( i );
    CHECK ( iSum == 100 );
    wprintf ( L"  forward : %u cells summing to %lld\n", l.count ( ), iSum );

    // Both ends are reachable by index, so GetTail()/GetTailPos() need no
    // spelling of their own.
    CHECK ( l.intAt ( 0 ) == 10 );
    CHECK ( l.intAt ( l.count ( ) - 1 ) == 40 );

    l.drop ( MSGF_DROP_TAIL );
    CHECK ( l.count ( ) == 3 );
    CHECK ( l.intAt ( 2 ) == 30 );

    l.drop ( MSGF_DROP_HEAD );
    CHECK ( l.count ( ) == 2 );
    CHECK ( l.intAt ( 0 ) == 20 );

    // Growth at the head.
    l.addInt ( 5, 0, MSGF_ADD_HEAD );
    CHECK ( l.count ( ) == 3 );
    CHECK ( l.intAt ( 0 ) == 5 );            // 5, 20, 30
    CHECK ( l.intAt ( 2 ) == 30 );

    // The BACKWARD walk the original demonstrated (GetPrev from GetTailPos)
    // has no facade verb, because a backward step needs the cell's address.
    // Counting down over the index is the same visit order, and is what a
    // caller writes instead.
    long long iBack = 0, iFirstBack = 0;
    for ( unsigned int i = l.count ( ); i-- > 0; )
    {
      if ( i == l.count ( ) - 1 ) iFirstBack = l.intAt ( i );
      iBack += l.intAt ( i );
    }
    CHECK ( iFirstBack == 30 );
    CHECK ( iBack == 55 );
    wprintf ( L"  backward: %u cells summing to %lld\n", l.count ( ), iBack );

    // A list is HETEROGENEOUS -- each cell carries its own type tag, and the
    // reader must ask before it reads.
    List mixed = root.declareList ( L"Mixed" );
    mixed.addInt  ( 7 );
    mixed.addReal ( 2.5 );
    mixed.addText ( L"seven and a half" );
    CHECK ( mixed.count ( ) == 3 );

    CHECK ( mixed.typeAt ( 0 ) == MSGF_TYPE_INT32 );
    CHECK ( mixed.typeAt ( 1 ) == MSGF_TYPE_DOUBLE );
    CHECK ( mixed.typeAt ( 2 ) == MSGF_TYPE_WSTR16 );
    CHECK ( mixed.intAt  ( 0 ) == 7 );
    CHECK ( mixed.realAt ( 1 ) == 2.5 );
    CHECK ( mixed.textAt ( 2 ) == L"seven and a half" );

    // GetTypeAt is the SAFE PROBE: past the end it answers NULL rather than
    // failing, so a caller can size a loop with it. Reading past the end is
    // MSGF_E_RANGE, which is a different question with a different answer.
    CHECK ( mixed.typeAt ( 99 ) == MSGF_TYPE_NULL );
    long long iJunk = 0;
    CHECK_HR ( mixed.get ( )->GetIntAt ( 99, &iJunk, 0 ), MSGF_E_RANGE );

    // An in-place write keeps the cell's declared type: a list is typed PER
    // CELL, and this is not the way to change one.
    mixed.setIntAt ( 0, 8 );
    CHECK ( mixed.intAt ( 0 ) == 8 );
    CHECK_HR ( mixed.get ( )->SetRealAt ( 0, 1.0 ), MSGF_E_TYPE );

    // A LIST CELL CANNOT GROW. A node's value lengthens freely
    // (IMsgNode::SetText); a list cell lives inside the list's own block, and
    // the core's grow path asserts its way out of there -- so the facade
    // refuses the write instead.
    mixed.setTextAt ( 2, L"shorter" );
    CHECK ( mixed.textAt ( 2 ) == L"shorter" );
    CHECK_HR ( mixed.get ( )->SetTextAt ( 2, L"a value far longer than the cell it must fit" )
             , MSGF_E_LIMIT );
    CHECK ( mixed.textAt ( 2 ) == L"shorter" );        // and left it alone

    mixed.deleteAt ( 1 );
    CHECK ( mixed.count ( ) == 2 );
    CHECK_HR ( mixed.get ( )->DeleteAt ( 99 ), MSGF_E_RANGE );

    mixed.truncate ( );
    CHECK ( mixed.count ( ) == 0 );
    CHECK_HR ( mixed.get ( )->Drop ( MSGF_DROP_HEAD ), MSGF_E_RANGE );

    // A list handle is a route too: it still works after the heap has moved.
    for ( int i = 0; i < 200; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"pad%03d", i );
      root.declareText ( szName, L"padding padding padding padding" );
    }
    l.addInt ( 99 );
    CHECK ( l.count ( ) == 4 );
    CHECK ( l.intAt ( 3 ) == 99 );
}


// =========================================================================
// 2. A vect -- a dense, indexed array
// =========================================================================
//
// Where a list holds values, a vect holds ELEMENTS: each is a node in its own
// right. It is declared with a length and an element PROTOTYPE that fixes each
// element's initial type -- the original's third constructor argument, here
// the `type` argument.
//
// Indexing is genuine random access (a vect is a slot table, not a chain), so
// unlike a list there is no reason to reach for a cursor.
//
static void Demo_Vect ( Store& st )
{
    light::Section ( L"2. a vect -- a dense, indexed array" );

    Node root = st.root ( );
    Vect v    = root.declareVect ( L"Reading", 3, MSGF_TYPE_INT32 );
    CHECK ( v.count ( ) == 3 );

    v.setIntAt ( 0, 100 );
    v.setIntAt ( 1, 200 );
    v.setIntAt ( 2, 300 );
    CHECK ( v.intAt ( 0 ) == 100 );
    CHECK ( v.intAt ( 2 ) == 300 );

    CHECK ( v.typeAt ( 0 ) == MSGF_TYPE_INT32 );
    CHECK ( v.kindAt ( 0 ) == MSGF_KIND_ITEM || v.kindAt ( 0 ) == MSGF_KIND_DATA );

    // The original's Goto() was the bounds check: non-zero in range, 0 out of
    // it, never a throw. Here the bound is reported by the operation itself.
    long long iJunk = 0;
    CHECK_HR ( v.get ( )->GetIntAt ( 3, &iJunk, 0 ), MSGF_E_RANGE );
    CHECK_HR ( v.get ( )->SetIntAt ( 3, 1 ),         MSGF_E_RANGE );

    // A vect of a different prototype.
    Vect vr = root.declareVect ( L"Ratios", 2, MSGF_TYPE_DOUBLE );
    vr.setRealAt ( 1, 2.5 );
    CHECK ( vr.realAt ( 1 ) == 2.5 );
    CHECK ( vr.typeAt ( 1 ) == MSGF_TYPE_DOUBLE );

    // Delete COMPACTS, exactly as the original's Delete() did.
    v.deleteAt ( 1 );
    CHECK ( v.count ( ) == 2 );
    CHECK ( v.intAt ( 0 ) == 100 );
    CHECK ( v.intAt ( 1 ) == 300 );                  // 200 is gone, 300 moved down
    CHECK_HR ( v.get ( )->DeleteAt ( 9 ), MSGF_E_RANGE );

    // The original's deep-copy demonstration (`oCopy = oVect;` giving two
    // independent vects) has no facade equivalent, and could not: a container
    // here is a HANDLE ONTO A PLACE in a store, not a value you can hold by
    // itself. Copying one means copying its elements into another place, which
    // is a loop -- and being explicit about that is the point.
    Vect vCopy = root.declareVect ( L"Copy", v.count ( ), MSGF_TYPE_INT32 );
    for ( unsigned int i = 0; i < v.count ( ); ++i )
      vCopy.setIntAt ( i, v.intAt ( i ) );
    CHECK ( vCopy.count ( ) == 2 );
    CHECK ( vCopy.intAt ( 0 ) == 100 );
    vCopy.setIntAt ( 0, 999 );
    CHECK ( v.intAt ( 0 ) == 100 );                  // independent storage

    vCopy.truncate ( );
    CHECK ( vCopy.count ( ) == 0 );

    // ...and a vect handle survives a mutation, like every other handle here.
    for ( int i = 0; i < 200; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"pad%03d", i );
      root.declareText ( szName, L"padding padding padding padding" );
    }
    CHECK ( v.intAt ( 1 ) == 300 );

    wprintf ( L"  vect of %u, and a hand-made copy that is independent\n", v.count ( ) );
}


// =========================================================================
// 3. The 32-element seam
// =========================================================================
//
// A vect keeps its first 32 element addresses inline (aAlloc[32]) and spills
// the rest into aExtra continuation blocks. Nothing above the API sees the
// difference -- which is exactly why it is worth an example, because it is
// where an indexed container usually goes wrong.
//
// The original crossed the seam with InsertAt. The facade has no insert, so
// this crosses it with the two verbs it does have: a declare that is already
// past the seam, and a delete that shifts every element across it.
//
static void Demo_VectSpill ( Store& st )
{
    light::Section ( L"3. a vect past its 32 inline slots" );

    Node root = st.root ( );
    Vect v    = root.declareVect ( L"Elem", 70, MSGF_TYPE_INT32 );
    CHECK ( v.count ( ) == 70 );

    for ( unsigned int i = 0; i < v.count ( ); ++i )
      v.setIntAt ( i, (long long)( i * 10 ) );

    CHECK ( v.intAt ( 0 )  == 0 );
    CHECK ( v.intAt ( 31 ) == 310 );         // last inline slot
    CHECK ( v.intAt ( 32 ) == 320 );         // first continuation slot
    CHECK ( v.intAt ( 63 ) == 630 );         // into the second block
    CHECK ( v.intAt ( 69 ) == 690 );

    // Delete exactly ON the seam: every element above it shifts down one, ACROSS
    // the block boundary.
    v.deleteAt ( 32 );
    CHECK ( v.count ( ) == 69 );
    CHECK ( v.intAt ( 31 ) == 310 );         // untouched, still inline
    CHECK ( v.intAt ( 32 ) == 330 );         // the former [33], pulled down
    CHECK ( v.intAt ( 68 ) == 690 );

    // ...and one BELOW it, which shifts the whole spill region.
    v.deleteAt ( 0 );
    CHECK ( v.count ( ) == 68 );
    CHECK ( v.intAt ( 0 )  == 10 );
    CHECK ( v.intAt ( 31 ) == 330 );         // what was [32] is now inline
    CHECK ( v.intAt ( 67 ) == 690 );

    wprintf ( L"  70 elements across 3 blocks, two seam deletes intact\n" );
}


// =========================================================================
// 4. Containment
// =========================================================================
//
// A node's child collection holds lists and vects alongside plain items, and
// the two open-an-existing-one verbs tell "not there" from "there, but not
// that kind" -- so a caller never has to pre-check with Exists.
//
static void Demo_Containment ( Store& st )
{
    light::Section ( L"4. containment -- containers among the children" );

    Node root   = st.root ( );
    Node holder = root.declareText ( L"Holder", L"" );

    List samples = holder.declareList ( L"Samples" );
    samples.addInt ( 11 );
    samples.addInt ( 22 );

    Vect payload = holder.declareVect ( L"Payload", 3, MSGF_TYPE_INT32 );
    payload.setIntAt ( 0, 5 );
    payload.setIntAt ( 1, 6 );
    payload.setIntAt ( 2, 7 );

    holder.declareText ( L"Label", L"mixed bag" );

    CHECK ( holder.exists ( L"Samples" ) );
    CHECK ( holder.exists ( L"Payload" ) );
    CHECK ( holder.count ( ) == 3 );

    // Re-open them by name, from a handle that never saw the declare.
    Node again = st.at ( L".Holder" );
    List backL = again.list ( L"Samples" );
    Vect backV = again.vect ( L"Payload" );
    CHECK ( backL.count ( ) == 2 );
    CHECK ( backV.count ( ) == 3 );
    CHECK ( backV.intAt ( 0 ) == 5 );
    CHECK ( backV.intAt ( 2 ) == 7 );

    // Absent, versus present-but-wrong-kind. Two questions, two codes.
    IMsgList *pL = 0;
    IMsgVect *pV = 0;
    CHECK_HR ( again.get ( )->GetList ( MSGF_SCOPE_CHILD, L"Ghost",   &pL ), MSGF_E_NO_ITEM );
    CHECK_HR ( again.get ( )->GetList ( MSGF_SCOPE_CHILD, L"Label",   &pL ), MSGF_E_TYPE );
    CHECK_HR ( again.get ( )->GetVect ( MSGF_SCOPE_CHILD, L"Samples", &pV ), MSGF_E_TYPE );

    // A container is also reachable as a NODE, which the kernel's own
    // SelectItem could not do -- it raises "a list is not an item" on both.
    // Every lookup in the facade is cursor-based for exactly this reason.
    Node asNode = again.child ( L"Samples" );
    CHECK ( asNode.kind ( ) == MSGF_KIND_LIST );
    CHECK ( asNode.path ( ) == L".Holder.Samples" );
    CHECK ( again.child ( L"Payload" ).kind ( ) == MSGF_KIND_VECT );
    CHECK ( again.child ( L"Label" ).kind ( )   == MSGF_KIND_ITEM );

    wprintf ( L"  Holder holds a %u-list, a %u-vect and a plain item\n"
            , backL.count ( ), backV.count ( ) );
}


// =========================================================================
// 5. Walking a collection you did not build
// =========================================================================
//
// The original's idiom was the one Msgcore uses internally:
//
//     P3PmsgCurs& oCurs = oDesc.r_Curs();
//     for (int i = 0; oCurs.Goto(i); i++) { ...ask what it is, then take it... }
//
// with the cursor OWNED BY THE COLLECTION -- not to be deleted, not to be kept
// across a change. Here the cursor is an object the caller owns and releases,
// and the loop is driven by IsEnd rather than by a Goto that doubles as a
// predicate. The core's own IsEoCursor answers TRUE while standing ON the last
// element, so a loop driven by IT visits every element but the last, and then
// ++ raises past the end. That trap is closed here.
//
static void Demo_Cursor ( Store& st )
{
    light::Section ( L"5. a cursor -- generic traversal" );

    Node root = st.root ( );
    Node tele = root.declareText ( L"Telemetry", L"" );

    tele.declareText ( L"Device", L"sensor-04" );
    tele.declareInt  ( L"Uptime", 86400 );

    List hist = tele.declareList ( L"History" );
    hist.addInt ( 1 ); hist.addInt ( 2 ); hist.addInt ( 3 );

    Vect win = tele.declareVect ( L"Window", 4, MSGF_TYPE_DOUBLE );
    for ( unsigned int i = 0; i < 4; ++i ) win.setRealAt ( i, i * 1.5 );

    CHECK ( tele.count ( ) == 4 );

    int nItems = 0, nLists = 0, nVects = 0, nSeen = 0;
    std::vector<std::wstring> vPaths;

    for ( Cursor c = tele.cursor ( ); !c.end ( ); c.next ( ) )
    {
      ++nSeen;
      const std::wstring strName = c.name ( );

      switch ( c.kind ( ) )
      {
        case MSGF_KIND_LIST:
          ++nLists;
          wprintf ( L"  [%u] LIST %-10s\n", c.index ( ), strName.c_str ( ) );
          break;
        case MSGF_KIND_VECT:
          ++nVects;
          wprintf ( L"  [%u] VECT %-10s\n", c.index ( ), strName.c_str ( ) );
          break;
        default:
          ++nItems;
          wprintf ( L"  [%u] ITEM %-10s\n", c.index ( ), strName.c_str ( ) );
          break;
      }

      // The bridge back out of the walk: a path-based node for the current
      // element, which DOES survive the mutation that would invalidate this
      // cursor. Collect them here, act on them after the loop.
      vPaths.push_back ( c.node ( ).path ( ) );
    }

    CHECK ( nSeen  == 4 );                   // INCLUDING the last element
    CHECK ( nItems == 2 );
    CHECK ( nLists == 1 );
    CHECK ( nVects == 1 );
    CHECK ( nItems + nLists + nVects == (int)tele.count ( ) );
    CHECK ( vPaths.size ( ) == 4 );
    CHECK ( vPaths[0] == L".Telemetry.Device" );

    // ...and now the mutation, safely after the walk has finished.
    //
    // Only the ITEMS take it, and that is a fact about the model rather than a
    // limitation of this loop: a list and a vect are containers, not items, so
    // they have no attribute collection to declare into and the facade
    // answers MSGF_E_TYPE rather than pretending. Which is the same reason the
    // kernel's own SelectItem raises on them.
    for ( size_t i = 0; i < vPaths.size ( ); ++i )
    {
      Node n = st.at ( vPaths[i] );
      const HRESULT hr = n.get ( )->DeclareText ( MSGF_SCOPE_ATTR, L"seen", L"yes", 0, 0 );
      CHECK_HR ( hr, n.kind ( ) == MSGF_KIND_ITEM ? S_OK : MSGF_E_TYPE );
    }
    CHECK ( st.at ( L".Telemetry.Device" ).child ( L"seen", Attr ).asText ( ) == L"yes" );
    CHECK ( st.at ( L".Telemetry.Uptime" ).child ( L"seen", Attr ).asText ( ) == L"yes" );
    // ...and asking a container HOW MANY attributes it has is the same
    // question, so it gets the same answer rather than a misleading 0.
    unsigned int uAttrs = 0;
    CHECK_HR ( st.at ( L".Telemetry.History" ).get ( )->GetCount ( MSGF_SCOPE_ATTR, &uAttrs )
             , MSGF_E_TYPE );

    // Positioning by name, and what a miss does to the position.
    Cursor c = tele.cursor ( );
    CHECK ( c.gotoName ( L"UPTIME" ) );                  // the core matches without case
    CHECK ( c.name ( ) == L"Uptime" );                   // and answers the STORED spelling
    const unsigned int uWas = c.index ( );
    CHECK ( !c.gotoName ( L"nosuch" ) );
    CHECK ( c.index ( ) == uWas );                       // a miss leaves it where it was

    // Deleting through the cursor is the one mutation a cursor survives.
    CHECK ( c.gotoName ( L"Device" ) );
    c.remove ( );
    CHECK ( !tele.exists ( L"Device" ) );
    CHECK ( c.count ( ) == 3 );
    CHECK ( !c.end ( ) );                                // still usable
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    light::InitConsole ( );
    wprintf ( L"=== ListVectTest (Light) - Msgcore containers through MsgFacade ===\n" );

    try
    {
      Library lib;

      { Store st = lib.createStore ( ); Demo_List        ( st ); }
      { Store st = lib.createStore ( ); Demo_Vect        ( st ); }
      { Store st = lib.createStore ( ); Demo_VectSpill   ( st ); }
      { Store st = lib.createStore ( ); Demo_Containment ( st ); }
      { Store st = lib.createStore ( ); Demo_Cursor      ( st ); }
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
