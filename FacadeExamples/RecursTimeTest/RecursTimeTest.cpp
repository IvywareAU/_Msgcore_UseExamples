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
// RecursTimeTest.cpp  (Light -- MsgFacade)
//
// The two parts of the model the other harnesses never reach: the RECURSIVE
// WALKER, and a node's TIMESTAMP. Same five sections as
// ..\DirectExamples\RecursTimeTest.
//
//   original                                 light (this file)
//   ---------------------------------------------------------------------
//   P2PmsgRecurs oRec(oRoot);                Walker w = node.walker()
//   while (!oRec.IsEoRecurs()) { ... ++oRec }  for (; !w.end(); w.next())
//   oRec.IsField() / IsList() / IsVect()     w.kind()
//   oRec.Push()  -- THROWS on a list         w.push() -- S_FALSE / MSGF_E_TYPE
//   oRec.Pop() / oRec.Break()                w.pop() / w.breakOut()
//   oRec.r_data().c_int()                    (see section 3: a walker reports
//                                             WHERE it is, and the value comes
//                                             from a node)
//   P3PmsgTime oTime(iWhen)                  node.setTime(iWhen)
//   oTime.DataType() == VBLockData_TIME64    node.time()
//
// TWO DIFFERENCES WORTH THE PARAGRAPH THEY COST:
//
//  * PUSH IS THE SAME IDEA WITH THREE ANSWERS INSTEAD OF TWO. The original's
//    Push() either descended or THREW ("Attempt to push non-P2PmsgItem
//    environment") -- so a generic walk over a tree containing a list had to
//    test IsField() first or be wrapped in a try. Here S_OK means it
//    descended, S_FALSE means "an item, but it has nothing under it", and
//    MSGF_E_TYPE means "not something that can be descended into at all". Only
//    S_OK moves the walker, so a caller that ignores the distinction still
//    walks correctly -- which is why section 1's loop can simply push at every
//    stop.
//
//  * THE TIMESTAMP IS NOT P3PmsgTime. The original's fourth section was about
//    a typed CELL: a P3PmsgData subclass carrying VBLockData_TIME64, so a
//    consumer could tell a timestamp from a plain 64-bit integer. The facade
//    does not expose that type -- what it exposes instead is a node's
//    timestamp, which the kernel holds as an attribute, through GetTime /
//    SetTime. So a time here is a PROPERTY OF A NODE rather than a KIND OF
//    VALUE, and any node can have one whatever its value type. Section 4 shows
//    what that buys and what it costs.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 1 = SETUP, 3 = at least one check failed.

#include "MsgFacadeFn.hpp"
#include "LightHarness.h"

#include <vector>
#include <string>

using namespace msgf;

// A walk that cannot hang. Every loop over a walker in this file is bounded: a
// traversal bug in the library should fail the harness, not wedge it, because
// a wedged test tells you nothing and blocks the build.
static const int kMaxSteps = 500;

// -------------------------------------------------------------------------
// The tree every section walks.
//
//   Company                     "Ivyware"
//     Product                   "Chartboard"
//       Version                 "3.1"
//       Build                   4210
//     Founded                   1996
//   Notes                       "misc"
//     Memo                      "hello"
//   Tags                        list  [10, 20]
//   Weekly                      vect  3 elements
//
// Depth-first pre-order, which is what a full walk produces, is therefore
//   Company Product Version Build Founded Notes Memo Tags Weekly
//
static void BuildTree ( Store& st )
{
    Node root = st.root ( );

    Node company = root.declareText    ( L"Company", L"Ivyware" );
    Node product = company.declareText ( L"Product", L"Chartboard" );
    product.declareText ( L"Version", L"3.1" );
    product.declareInt  ( L"Build",   4210 );
    company.declareInt  ( L"Founded", 1996 );

    Node notes = root.declareText ( L"Notes", L"misc" );
    notes.declareText ( L"Memo", L"hello" );

    List tags = root.declareList ( L"Tags" );
    tags.addInt ( 10 );
    tags.addInt ( 20 );

    Vect weekly = root.declareVect ( L"Weekly", 3, MSGF_TYPE_INT32 );
    for ( unsigned int i = 0; i < 3; ++i )
      weekly.setIntAt ( i, (long long)( ( i + 1 ) * 100 ) );
}

static std::wstring Join ( const std::vector<std::wstring>& v )
{
    std::wstring s;
    for ( size_t i = 0; i < v.size ( ); ++i )
    {
      if ( i ) s += L" ";
      s += v[i];
    }
    return s;
}


// =========================================================================
// 1. One flat loop over a whole subtree
// =========================================================================
//
// A walker is opened over a node's collection, so the node itself is not
// visited -- exactly like a cursor, which it is built from. It owns a CHAIN of
// cursors, one per level, and splices descent into its advance.
//
// What it does NOT do is descend on its own: the caller decides, by calling
// push(). That is what makes it a walker rather than an iterator, and what
// lets section 2 prune a branch by simply not descending into it.
//
static void Demo_WalkWholeTree ( Store& st )
{
    light::Section ( L"1. a walker -- one flat loop over a whole subtree" );

    BuildTree ( st );
    Node root = st.root ( );

    std::vector<std::wstring> vSeen;
    int nSteps = 0;

    for ( Walker w = root.walker ( ); !w.end ( ); w.next ( ) )
    {
      if ( ++nSteps > kMaxSteps ) { CHECK ( false ); break; }
      vSeen.push_back ( w.name ( ) );
      w.push ( );                    // descend wherever it is possible
    }

    wprintf ( L"  visited : %s\n", Join ( vSeen ).c_str ( ) );

    CHECK ( vSeen.size ( ) == 9 );
    if ( vSeen.size ( ) == 9 )
    {
      CHECK ( vSeen[0] == L"Company" );
      CHECK ( vSeen[1] == L"Product" );
      CHECK ( vSeen[2] == L"Version" );    // depth 2 -- two pushes below the root
      CHECK ( vSeen[3] == L"Build" );
      CHECK ( vSeen[4] == L"Founded" );    // back up one level, on its own
      CHECK ( vSeen[5] == L"Notes" );      // back up two
      CHECK ( vSeen[6] == L"Memo" );
      CHECK ( vSeen[7] == L"Tags" );       // a list: push() answered MSGF_E_TYPE
      CHECK ( vSeen[8] == L"Weekly" );     // a vect: likewise
    }

    // The unwinding is the part worth noticing. Nothing in that loop pops:
    // next() finds the deepest cursor, sees it is spent, pops it and retries --
    // so "Founded" and "Notes" cost the caller nothing.

    // A one-level cursor over the same node sees only the four top-level
    // names, which is the difference the walker exists for.
    int nTop = 0;
    for ( Cursor c = root.cursor ( ); !c.end ( ); c.next ( ) )
      ++nTop;
    CHECK ( nTop == 4 );
    CHECK ( (int)vSeen.size ( ) > nTop );
    wprintf ( L"  one level: %d names, whole subtree: %d\n", nTop, (int)vSeen.size ( ) );
}


// =========================================================================
// 2. Push, pop and break -- the caller drives the descent
// =========================================================================
static void Demo_PushPopBreak ( Store& st )
{
    light::Section ( L"2. push / pop / break -- driving the descent by hand" );

    BuildTree ( st );
    Node root = st.root ( );

    // Not descending at all reduces the walker to a one-level cursor.
    {
      std::vector<std::wstring> v;
      int nSteps = 0;
      for ( Walker w = root.walker ( ); !w.end ( ); w.next ( ) )
      {
        if ( ++nSteps > kMaxSteps ) { CHECK ( false ); break; }
        v.push_back ( w.name ( ) );     // no push -- stay on the top level
      }
      CHECK ( v.size ( ) == 4 );
      CHECK ( Join ( v ) == L"Company Notes Tags Weekly" );
    }

    // Push reports the level it entered, 1-based; pop the level it came back
    // to. Both through the raw interface, since the sugar hides the number.
    {
      IMsgWalker *pW = 0;
      CHECK_HR ( root.get ( )->OpenWalker ( MSGF_SCOPE_CHILD, &pW ), S_OK );

      wchar_t szName[MAX_NAME + 1]; unsigned int cch = MAX_NAME + 1;
      pW->GetName ( szName, &cch );
      CHECK ( ::wcscmp ( szName, L"Company" ) == 0 );

      int nDepth = -1;
      CHECK_HR ( pW->Push ( &nDepth ), S_OK );
      CHECK ( nDepth == 1 );
      CHECK_HR ( pW->Next ( ), S_OK );               // lands on Company's first child
      cch = MAX_NAME + 1; pW->GetName ( szName, &cch );
      CHECK ( ::wcscmp ( szName, L"Product" ) == 0 );

      CHECK_HR ( pW->Push ( &nDepth ), S_OK );
      CHECK ( nDepth == 2 );
      CHECK_HR ( pW->Next ( ), S_OK );
      cch = MAX_NAME + 1; pW->GetName ( szName, &cch );
      CHECK ( ::wcscmp ( szName, L"Version" ) == 0 );

      // Pop abandons the rest of Product's children and returns to the level
      // that pushed. It always pops the DEEPEST open level, however deep the
      // chain, so this is the one on Product.
      CHECK_HR ( pW->Pop ( &nDepth ), S_OK );
      CHECK ( nDepth == 1 );
      cch = MAX_NAME + 1; pW->GetName ( szName, &cch );
      CHECK ( ::wcscmp ( szName, L"Product" ) == 0 );

      CHECK_HR ( pW->Pop ( &nDepth ), S_OK );
      CHECK ( nDepth == 0 );
      CHECK_HR ( pW->Pop ( &nDepth ), MSGF_E_RANGE );  // already outermost
      pW->Release ( );
    }

    // PUSH'S THREE ANSWERS, which is the whole difference from the original.
    {
      IMsgWalker *pW = 0;
      root.get ( )->OpenWalker ( MSGF_SCOPE_CHILD, &pW );

      wchar_t szName[MAX_NAME + 1]; unsigned int cch = MAX_NAME + 1;

      // (a) S_OK -- an item with children.
      CHECK_HR ( pW->Push ( 0 ), S_OK );
      pW->Break ( );

      // (b) MSGF_E_TYPE -- a list. The original THREW here, which is why its
      //     generic walk had to test IsField() before every descent.
      int nSteps = 0;
      for ( ;; )
      {
        cch = MAX_NAME + 1;
        if ( FAILED ( pW->GetName ( szName, &cch ) ) ) break;
        if ( ::wcscmp ( szName, L"Tags" ) == 0 ) break;
        if ( ++nSteps > kMaxSteps || pW->Next ( ) != S_OK ) break;
      }
      CHECK ( ::wcscmp ( szName, L"Tags" ) == 0 );
      unsigned int uKind = 0; pW->GetKind ( &uKind );
      CHECK ( uKind == MSGF_KIND_LIST );
      CHECK_HR ( pW->Push ( 0 ), MSGF_E_TYPE );
      int nDepth = -1; pW->GetDepth ( &nDepth );
      CHECK ( nDepth == 0 );                          // and it did not move
      pW->Release ( );
    }

    // (c) S_FALSE -- an item that simply has nothing under it.
    {
      Node leaf = st.at ( L".Company.Founded" );
      CHECK ( leaf.count ( ) == 0 );

      IMsgWalker *pW = 0;
      root.get ( )->OpenWalker ( MSGF_SCOPE_CHILD, &pW );
      pW->Push ( 0 );                                 // into Company
      pW->Next ( );                                   // Product
      pW->Next ( );                                   // Founded
      wchar_t szName[MAX_NAME + 1]; unsigned int cch = MAX_NAME + 1;
      pW->GetName ( szName, &cch );
      CHECK ( ::wcscmp ( szName, L"Founded" ) == 0 );
      CHECK_HR ( pW->Push ( 0 ), S_FALSE );
      pW->Release ( );
    }

    // Break abandons every pushed level at once and resumes at the outermost
    // -- and the walk CONTINUES from there rather than ending, which is the
    // one behavioural difference from the original's Break() (that one ran the
    // top cursor off its end, so IsEoRecurs() was true straight after).
    {
      IMsgWalker *pW = 0;
      root.get ( )->OpenWalker ( MSGF_SCOPE_CHILD, &pW );
      pW->Push ( 0 ); pW->Next ( );
      pW->Push ( 0 ); pW->Next ( );
      int nDepth = 0; pW->GetDepth ( &nDepth );
      CHECK ( nDepth == 2 );

      CHECK_HR ( pW->Break ( ), S_OK );
      pW->GetDepth ( &nDepth );
      CHECK ( nDepth == 0 );

      CHECK_HR ( pW->Next ( ), S_OK );
      wchar_t szName[MAX_NAME + 1]; unsigned int cch = MAX_NAME + 1;
      pW->GetName ( szName, &cch );
      CHECK ( ::wcscmp ( szName, L"Notes" ) == 0 );   // the next SIBLING
      pW->Release ( );
    }
}


// =========================================================================
// 3. Reading the tree through a walk
// =========================================================================
//
// The original's walker forwarded every accessor down the chain to the deepest
// open cursor, so oRec.r_data().c_int() read the current node's VALUE.
//
// This one does not, and the omission is the same one IMsgCursor makes on
// purpose: a walker reports WHERE IT IS -- kind, name, depth -- and a value
// comes from a node, which is a thing that survives the mutation a walker does
// not. So the idiom is to build each stop's PATH from the depth the walker
// reports, and read (or later write) through that.
//
// Which is nine lines, once, and is what a caller wanted anyway: the paths
// outlive the walk.
//
static void Demo_WalkExposure ( Store& st )
{
    light::Section ( L"3. reading the tree through a walk" );

    BuildTree ( st );
    Node root = st.root ( );

    int nItems = 0, nLists = 0, nVects = 0, nSteps = 0, nMaxDepth = 0;
    std::vector<std::wstring> vAncestry;      // the names down to here
    std::vector<std::wstring> vPaths;         // one per stop, in visit order

    for ( Walker w = root.walker ( ); !w.end ( ); w.next ( ) )
    {
      if ( ++nSteps > kMaxSteps ) { CHECK ( false ); break; }

      const int nDepth = w.depth ( );
      if ( nDepth > nMaxDepth ) nMaxDepth = nDepth;

      // The stop's path is its ancestors' names plus its own. depth() is
      // 0-based, so at depth d the first d entries are still the ancestry.
      vAncestry.resize ( (size_t)nDepth );
      vAncestry.push_back ( w.name ( ) );

      std::wstring strPath;
      for ( size_t i = 0; i < vAncestry.size ( ); ++i )
        strPath += L"." + vAncestry[i];
      vPaths.push_back ( strPath );

      switch ( w.kind ( ) )
      {
        case MSGF_KIND_LIST: ++nLists; break;
        case MSGF_KIND_VECT: ++nVects; break;
        default:             ++nItems; break;
      }

      w.push ( );
    }

    CHECK ( nItems == 7 );        // Company Product Version Build Founded Notes Memo
    CHECK ( nLists == 1 );        // Tags
    CHECK ( nVects == 1 );        // Weekly
    CHECK ( nMaxDepth == 2 );

    CHECK ( vPaths.size ( ) == 9 );
    CHECK ( vPaths[2] == L".Company.Product.Version" );
    CHECK ( vPaths[4] == L".Company.Founded" );
    CHECK ( vPaths[6] == L".Notes.Memo" );

    // ...and every one of them resolves, which is the check that says the
    // reconstruction is right rather than merely plausible.
    long long iBuild = 0;
    std::wstring strVersion;
    for ( size_t i = 0; i < vPaths.size ( ); ++i )
    {
      Node n = st.at ( vPaths[i] );
      if ( n.name ( ) == L"Build" )   iBuild     = n.asInt ( );
      if ( n.name ( ) == L"Version" ) strVersion = n.asText ( );
    }
    CHECK ( iBuild == 4210 );                    // read three levels down
    CHECK ( strVersion == L"3.1" );

    wprintf ( L"  %d items, %d list, %d vect, max depth %d; Build=%lld Version=%s\n"
            , nItems, nLists, nVects, nMaxDepth, iBuild, strVersion.c_str ( ) );

    // The paths outlive the walk, so the mutation the walker forbids is fine
    // once it has finished.
    for ( size_t i = 0; i < vPaths.size ( ); ++i )
    {
      Node n = st.at ( vPaths[i] );
      if ( n.kind ( ) == MSGF_KIND_ITEM )
        n.declareInt ( L"depth", (long long)i, Attr );
    }
    CHECK ( st.at ( L".Notes.Memo" ).child ( L"depth", Attr ).asInt ( ) == 6 );
}


// =========================================================================
// 4. A node's timestamp
// =========================================================================
//
// The kernel keeps a node's time as an attribute of the node rather than as
// its value, and that is what GetTime / SetTime reach. So:
//
//   * ANY node can carry one, whatever its own value type -- which the
//     original's P3PmsgTime could not do, being a value type itself;
//   * a node that was never stamped answers 0, so "unset" is a value rather
//     than a separate IsNull question;
//   * and the cost: a caller cannot tell a TIME64 VALUE from an INT64 value,
//     because the facade does not expose a time-typed cell at all. If a
//     timestamp has to be the value rather than a property, store it as INT64
//     and put the units in an attribute.
//
static void Demo_Time ( Store& st )
{
    light::Section ( L"4. a node's timestamp" );

    Node root = st.root ( );

    // A fixed instant, so the harness is deterministic: 2026-08-09 14:30:00Z
    // as seconds since the epoch.
    const long long iWhen = 1786379400LL;

    Node created = root.declareText ( L"Created", L"a record" );
    CHECK ( created.time ( ) == 0 );             // never stamped

    const long long iBack = created.setTime ( iWhen );
    CHECK ( iBack == iWhen );                    // reports what it stored
    CHECK ( created.time ( ) == iWhen );

    // The node's own value is untouched by any of that -- the two live in
    // different collections.
    CHECK ( created.asText ( ) == L"a record" );
    CHECK ( created.type ( ) == MSGF_TYPE_WSTR16 );

    // Any node, whatever it holds.
    Node count = root.declareInt ( L"Count", 7 );
    count.setTime ( iWhen + 3600 );
    CHECK ( count.time ( ) == iWhen + 3600 );
    CHECK ( count.asInt ( ) == 7 );

    // -1 means "now", and it reports back what was stored rather than making
    // the caller ask again.
    Node touched = root.declareText ( L"Touched", L"" );
    const long long iNow = touched.setTime ( );
    CHECK ( iNow > 1700000000LL );               // some time after 2023
    CHECK ( touched.time ( ) == iNow );

    // Re-stamping replaces rather than accumulating.
    touched.setTime ( iWhen );
    CHECK ( touched.time ( ) == iWhen );

    wprintf ( L"  Created stamped %lld, Touched now = %lld\n", iWhen, iNow );
}


// =========================================================================
// 5. A timestamp in a store, and through Save/Load
// =========================================================================
//
// The stamp is part of the heap image, which is what makes a stored timestamp
// still a timestamp after a round trip to disk -- the same property the
// original checked of its TIME64 tag.
//
static void Demo_TimeInTree ( Library& lib )
{
    light::Section ( L"5. a timestamp through save / load" );

    const std::wstring strFile = light::TempPath ( L"mscs_light_time" );
    ::DeleteFileW ( strFile.c_str ( ) );

    const long long iWhen = 1786379400LL;

    {
      Store st   = lib.createStore ( MSGF_ADDR_64, 4096, 1u << 20 );
      Node  root = st.root ( );

      Node created = root.declareText ( L"Created", L"a record" );
      created.setTime ( iWhen );
      root.declareText ( L"Label", L"unstamped" );

      CHECK ( created.time ( ) == iWhen );
      st.save ( strFile, MSGF_SAVE_DEFRAGMENT );
    }

    {
      Store st = lib.openStore ( strFile );
      CHECK ( st.ok ( ) );

      Node created = st.at ( L".Created" );
      CHECK ( created.time ( ) == iWhen );          // the stamp survived
      CHECK ( created.asText ( ) == L"a record" );  // and so did the value
      CHECK ( st.at ( L".Label" ).time ( ) == 0 );  // and "never stamped" too

      wprintf ( L"  reloaded: Created stamped %lld, Label unstamped\n"
              , created.time ( ) );
    }

    ::DeleteFileW ( strFile.c_str ( ) );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    light::InitConsole ( );
    wprintf ( L"=== RecursTimeTest (Light) - walking a subtree, and node timestamps ===\n" );

    try
    {
      Library lib;

      { Store st = lib.createStore ( MSGF_ADDR_64, 8192, 1u << 20 ); Demo_WalkWholeTree ( st ); }
      { Store st = lib.createStore ( MSGF_ADDR_64, 8192, 1u << 20 ); Demo_PushPopBreak  ( st ); }
      { Store st = lib.createStore ( MSGF_ADDR_64, 8192, 1u << 20 ); Demo_WalkExposure  ( st ); }
      { Store st = lib.createStore ( MSGF_ADDR_64, 4096, 1u << 20 ); Demo_Time          ( st ); }
      Demo_TimeInTree ( lib );
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
