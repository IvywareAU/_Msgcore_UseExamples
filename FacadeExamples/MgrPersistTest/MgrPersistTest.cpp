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
// MgrPersistTest.cpp  (Light -- MsgFacade)
//
// The Msgcore STORE: a heap that owns a whole tree, can be written to a file
// and read back, addresses any node by a stable position, and fires change
// notifications. Same four sections as
// ..\DirectExamples\MgrPersistTest.
//
//   original                                 light (this file)
//   ---------------------------------------------------------------------
//   P2PmsgMgr oMgr(VBLock_Addr64,4096,1<<20) lib.createStore(MSGF_ADDR_64,4096,1<<20)
//   oMgr.r_Desc(P3PmsgField::AttrCMD_Create) (gone -- declares make the
//                                             collection they need)
//   oMgr.Save(path, /*bDefragment*/ true)    st.save(path, MSGF_SAVE_DEFRAGMENT)
//   P2PmsgMgr oLoaded(path)  (ctor == Load)  lib.openStore(path)
//   oLive.GetP2Pos()                         node.pos()
//   oMgr.P2Pos2Field(pos) -> a DETACHED copy st.at(pos)  -> a live PATH node
//   oMgr.P2Pos2Path(pos)  -> "..Stock.SKU"   node.path() -> ".Stock.SKU"
//   oMgr.RootPath2Object(path) -- THROWS on  st.at(path) -- MSGF_E_NO_ITEM on
//     a path that names nothing                a path that names nothing
//   SetTriggerSink + CreateTrigger + Fire     st.events / st.arm / st.fire
//     + DropTriggers (4 calls, one HWND         (the HWND leg is gone: there
//     argument that must be NULL)               is no window in this API)
//
// THE ONE THING WORTH READING TWICE is what a position becomes. In the
// original, P2Pos2Field handed back a DETACHED DEEP COPY: fine to read, and
// writes through it were silently discarded. Here NodeFromPos searches the
// tree once and hands back a PATH node -- an ordinary, live, writable one --
// so "look something up by its inode number and then change it" is expressible
// at all, which in the original it was not.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 1 = SETUP, 3 = at least one check failed.

#include "MsgFacadeFn.hpp"
#include "LightHarness.h"

using namespace msgf;

// Build the same little tree in any store, so the save and the reload can be
// compared against one description.
static void BuildInventory ( Store& st )
{
    Node root = st.root ( );

    root.declareText ( L"Warehouse", L"Melbourne-01" );
    root.declareInt  ( L"Capacity",  50000 );
    root.declareReal ( L"Utilised",  0.735 );

    Node stock = root.declareText ( L"Stock", L"" );
    stock.declareInt ( L"SKU-1001", 412 );
    stock.declareInt ( L"SKU-1002", 88 );
    stock.declareInt ( L"SKU-1003", 0 );

    // A vect goes in whole; the heap stores its element addresses as image
    // offsets, so the array survives serialisation intact.
    Vect daily = root.declareVect ( L"DailyOut", 7, MSGF_TYPE_INT32 );
    for ( unsigned int i = 0; i < 7; ++i )
      daily.setIntAt ( i, (long long)( ( i + 1 ) * 11 ) );
}


// =========================================================================
// 1. A store, and what it reports about itself
// =========================================================================
//
// createStore(addr, initialBytes, maxBytes) picks the heap's address width and
// its growth bounds. MSGF_ADDR_64 is the safe default for anything that might
// get large; 16 and 32 buy compactness at a size ceiling (BstrWidthTest is
// about that knob). The heap grows on demand from initialBytes up to maxBytes
// and no further.
//
static void Demo_BuildAndInspect ( Library& lib )
{
    light::Section ( L"1. a store -- a heap that owns a tree" );

    Store st = lib.createStore ( MSGF_ADDR_64, 4096, 1u << 20 );
    CHECK ( st.ok ( ) );

    // A BRAND-NEW STORE IS ALREADY DIRTY, before anything is put in it: the
    // flag means "there are bytes here that no file has", and building the
    // empty heap and its root made some. So dirty() is the answer to "would a
    // save write something", not to "have I changed anything since I made it".
    CHECK ( st.dirty ( ) );

    // size() is the heap's CURRENT ALLOCATION, not the bytes in use. It starts
    // at initialBytes (plus the header) and only moves when the tree outgrows
    // it -- so a small tree in a 4 KB heap reports 4 KB before and after. Do
    // not read it as "how big is my data".
    const unsigned int uEmpty = st.size ( );
    BuildInventory ( st );
    const unsigned int uFull = st.size ( );

    CHECK ( uFull >= uEmpty );
    CHECK ( st.dirty ( ) );                          // unsaved changes are tracked

    Node root = st.root ( );
    CHECK ( root.exists ( L"Warehouse" ) );
    CHECK ( root.exists ( L"Stock" ) );
    CHECK ( root.count ( ) == 5 );
    CHECK ( root.child ( L"Capacity" ).asInt ( ) == 50000 );
    CHECK ( root.child ( L"Stock" ).count ( ) == 3 );

    wprintf ( L"  heap %u -> %u bytes for 5 top-level nodes (fits the initial 4 KB)\n"
            , uEmpty, uFull );

    // Force the growth path: a deliberately tiny initial heap, filled past it.
    // (The obvious name for this store is `small`, which <rpcndr.h> has
    // #defined to `char` since 1993. Every Windows client gets to find that
    // out once.)
    Store tiny = lib.createStore ( MSGF_ADDR_64, 512, 1u << 20 );
    const unsigned int uStart = tiny.size ( );
    Node sroot = tiny.root ( );
    for ( int i = 0; i < 300; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"node%03d", i );
      sroot.declareInt ( szName, i );
    }
    CHECK ( tiny.size ( ) > uStart );
    CHECK ( sroot.count ( ) == 300 );
    CHECK ( sroot.child ( L"node299" ).asInt ( ) == 299 );
    wprintf ( L"  heap %u -> %u bytes after 300 nodes in a 512-byte start\n"
            , uStart, tiny.size ( ) );

    // setDirty exists because a host that has just written its own journal may
    // want to say the store is clean without saving it.
    st.setDirty ( false );
    CHECK ( !st.dirty ( ) );
}


// =========================================================================
// 2. Save and load
// =========================================================================
//
// A save writes the heap image and then HOLDS THE FILE OPEN for the store's
// lifetime (share mode FILE_SHARE_READ). Other readers may open it; other
// writers may not. That is why the writer below lives in its own scope.
//
// MSGF_SAVE_DEFRAGMENT compacts the heap on the way out, which is what you
// want for a store that has seen a lot of deletes.
//
static void Demo_SaveLoad ( Library& lib )
{
    light::Section ( L"2. save / load -- the store as a document" );

    const std::wstring strPath = light::TempPath ( L"mscs_light_inventory" );
    ::DeleteFileW ( strPath.c_str ( ) );

    unsigned int uSaved = 0;

    // ---- writer ---------------------------------------------------------
    {
      Store st = lib.createStore ( MSGF_ADDR_64, 4096, 1u << 20 );
      BuildInventory ( st );

      st.save ( strPath, MSGF_SAVE_DEFRAGMENT );
      uSaved = st.size ( );

      CHECK ( !st.dirty ( ) );                       // saving clears it
      CHECK ( st.filename ( ).find ( L"mscs_light_inventory" ) != std::wstring::npos );
      wprintf ( L"  saved  : %s\n", st.filename ( ).c_str ( ) );
    }   // the writer closes the file here

    // ---- reader ---------------------------------------------------------
    {
      Store st = lib.openStore ( strPath );
      CHECK ( st.ok ( ) );
      CHECK ( st.size ( ) == uSaved );

      Node root = st.root ( );

      // Every scalar came back with its type and its value intact.
      CHECK ( root.exists ( L"Warehouse" ) );
      CHECK ( root.child ( L"Warehouse" ).asText ( ) == L"Melbourne-01" );
      CHECK ( root.child ( L"Capacity" ).asInt ( )  == 50000 );
      CHECK ( root.child ( L"Utilised" ).asReal ( ) == 0.735 );
      CHECK ( root.child ( L"Capacity" ).type ( ) == MSGF_TYPE_INT32 );

      // ...and so did the nested subtree.
      CHECK ( st.at ( L".Stock.SKU-1002" ).asInt ( ) == 88 );

      // ...and the vect, whose internal element addresses had to be re-based
      // against the loaded image.
      Vect daily = root.vect ( L"DailyOut" );
      CHECK ( daily.count ( ) == 7 );
      CHECK ( daily.intAt ( 0 ) == 11 );
      CHECK ( daily.intAt ( 6 ) == 77 );

      wprintf ( L"  loaded : %u nodes, DailyOut[0..6] = %lld..%lld\n"
              , root.count ( ), daily.intAt ( 0 ), daily.intAt ( 6 ) );
    }

    // ---- round two: mutate the reloaded store and save it again ---------
    {
      Store st = lib.openStore ( strPath );
      st.at ( L".Stock" ).declareInt ( L"SKU-1004", 7 );
      st.at ( L".Capacity" ).set ( (long long)60000 );
      CHECK ( st.dirty ( ) );
      st.save ( );                                   // no filename == over itself
      CHECK ( !st.dirty ( ) );
    }
    {
      Store st = lib.openStore ( strPath );
      CHECK ( st.at ( L".Capacity" ).asInt ( ) == 60000 );
      CHECK ( st.at ( L".Stock" ).count ( ) == 4 );
    }

    // A file that is not a store, and one that is not there. Both are
    // MSGF_E_FILE -- and the second one matters, because the core's own
    // P2PmsgMgr::IsValid() answers TRUE for a manager built from a missing
    // file (it is asking about the heap, which is a perfectly good empty one).
    {
      HRESULT hrOpen = S_OK;
      try                      { lib.openStore ( L"no-such-file-anywhere.p2p" ); }
      catch ( const Error& e ) { hrOpen = e.code ( ); }
      CHECK ( hrOpen == MSGF_E_FILE );
    }

    // ---- a node held ACROSS a Load --------------------------------------
    //
    // This is the test a raw kernel handle could not pass: Load replaces the
    // entire heap, so every address in it is gone. A route is not an address.
    {
      Store st   = lib.createStore ( );
      Node  root = st.root ( );
      root.declareText ( L"Warehouse", L"before the load" );

      Node held = root.child ( L"Warehouse" );
      CHECK ( held.asText ( ) == L"before the load" );

      st.load ( strPath );

      CHECK ( held.asText ( ) == L"Melbourne-01" );  // the NEW tree's node
      CHECK ( root.exists ( L"Stock" ) );            // the root node too
      wprintf ( L"  a node held across a Load now names '%s'\n"
              , held.asText ( ).c_str ( ) );
    }

    ::DeleteFileW ( strPath.c_str ( ) );
}


// =========================================================================
// 3. Positions and paths -- two ways to name a node
// =========================================================================
//
// A position is the store's natural inode number: an offset within the heap
// image, stable across Save/Load, storable, and what a trigger reports. It is
// what the P2P FileSystem hands out as an inode.
//
// It is NOT an identity: delete the node and the position can be handed out
// again to a later allocation. The facade's answer to that is to make holding
// a NODE across a mutation safe, so that a caller never needs to hold a
// position -- and to make NodeFromPos hand back a node rather than a copy.
//
static void Demo_Addressing ( Library& lib )
{
    light::Section ( L"3. positions and paths -- naming a node two ways" );

    Store st = lib.createStore ( MSGF_ADDR_64, 4096, 1u << 20 );
    BuildInventory ( st );

    Node live = st.at ( L".Stock.SKU-1002" );
    const unsigned long long uPos = live.pos ( );
    CHECK ( uPos != 0 );

    // ...and back the other way. In the original this was a detached copy that
    // could be read and not written; here it is a node like any other.
    Node byPos = st.at ( uPos );
    CHECK ( byPos.name ( ) == L"SKU-1002" );
    CHECK ( byPos.asInt ( ) == 88 );
    CHECK ( byPos.path ( ) == L".Stock.SKU-1002" );

    byPos.set ( (long long)89 );                     // a write through it LANDS
    CHECK ( st.at ( L".Stock.SKU-1002" ).asInt ( ) == 89 );
    byPos.set ( (long long)88 );

    wprintf ( L"  pos %llu -> path '%s'\n", uPos, byPos.path ( ).c_str ( ) );

    // The path round-trips exactly, which is what makes it a LOCATOR rather
    // than a rendering. (The core's own path spelling is built by walking
    // parent links and is not reversible into scopes, which is why the facade
    // does not use it.)
    CHECK ( st.at ( live.path ( ) ).asInt ( ) == 88 );
    CHECK ( st.at ( live.path ( ) ).pos ( ) == uPos );

    // An ATTRIBUTE has a position and a path too, and the '@' says which
    // collection it came from.
    live.declareText ( L"unit", L"cartons", Attr );
    Node attr = live.child ( L"unit", Attr );
    CHECK ( attr.path ( ) == L".Stock.SKU-1002@unit" );
    CHECK ( st.at ( attr.pos ( ) ).path ( ) == L".Stock.SKU-1002@unit" );
    CHECK ( st.at ( L".Stock.SKU-1002@unit" ).asText ( ) == L"cartons" );

    // A miss is an ANSWER, in both spellings. The original's RootPath2Object
    // signalled it by THROWING, which is why WsaQueryTest had to wrap every
    // lookup -- an exception escaping a hub handler drops the connection.
    IMsgNode *pNode = 0;
    CHECK_HR ( st.get ( )->NodeFromPath ( L".Stock.Nothing", &pNode ), MSGF_E_NO_ITEM );
    CHECK_HR ( st.get ( )->NodeFromPos  ( 0xDEADBEEF, &pNode ),        MSGF_E_NO_POS );
    CHECK_HR ( st.get ( )->NodeFromPos  ( 0, &pNode ),                 MSGF_E_NO_POS );

    // A position survives the round trip to disk.
    const std::wstring strPath = light::TempPath ( L"mscs_light_pos" );
    ::DeleteFileW ( strPath.c_str ( ) );
    st.save ( strPath );
    {
      Store re = lib.openStore ( strPath );
      CHECK ( re.at ( L".Stock.SKU-1002" ).pos ( ) == uPos );
      wprintf ( L"  reloaded, SKU-1002 still at pos %llu\n", uPos );
    }
    ::DeleteFileW ( strPath.c_str ( ) );
}


// =========================================================================
// 4. Change notification, without a window
// =========================================================================
//
// Msgcore's original trigger path posts a Windows message to an HWND, which is
// unreachable from a service, a daemon, a FUSE mount or a console harness. The
// original harness worked around that by passing (HWND)nullptr and relying on
// the manager's second, function-pointer sink.
//
// The facade exposes only the windowless path, so the HWND argument -- and the
// question of what to pass for it -- is gone. Arming is still per NODE and per
// MASK.
//
// THE DELIVERY CONTRACT IS NOT THIS ABI'S USUAL ONE, and the header says so:
// the callback arrives synchronously, on the mutating thread, with the store's
// lock HELD. Calling back into the store from inside it deadlocks. Copy the
// position and post it to your own queue -- which is what this sink does.
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

static void Demo_Triggers ( Library& lib )
{
    light::Section ( L"4. change notification -- headless triggers" );

    Store st = lib.createStore ( MSGF_ADDR_64, 4096, 1u << 20 );
    BuildInventory ( st );

    const unsigned long long uWatched = st.at ( L".Stock.SKU-1001" ).pos ( );
    CHECK ( uWatched != 0 );

    Capture oCap;
    st.events ( &oCap );
    st.arm ( uWatched, MSGF_TRIG_UPDATE | MSGF_TRIG_INSERT );

    // Fire UPDATE by hand -- the host telling the store "this node changed".
    CHECK ( st.fire ( uWatched, MSGF_TRIG_UPDATE ) == 1 );
    CHECK ( oCap.m_nCount == 1 );
    CHECK ( oCap.m_uLastType == MSGF_TRIG_UPDATE );
    CHECK ( oCap.m_uLastPos == uWatched );

    // The INSERT arm is a separate registration on the same node.
    CHECK ( st.fire ( uWatched, MSGF_TRIG_INSERT ) == 1 );
    CHECK ( oCap.m_nCount == 2 );
    CHECK ( oCap.m_uLastType == MSGF_TRIG_INSERT );

    // An unarmed mask delivers nothing. (Do not use DELETE as a probe: a
    // DELETE pass is read as "the object is gone" and drops every registration
    // on the node.)
    CHECK ( st.fire ( uWatched, MSGF_TRIG_ACTIVE ) == 0 );
    CHECK ( oCap.m_nCount == 2 );

    // Disarm the UPDATE leg; INSERT survives it.
    st.disarm ( uWatched, MSGF_TRIG_UPDATE );
    CHECK ( st.fire ( uWatched, MSGF_TRIG_UPDATE ) == 0 );
    CHECK ( st.fire ( uWatched, MSGF_TRIG_INSERT ) == 1 );
    CHECK ( oCap.m_nCount == 3 );

    // The two arguments that are refused rather than ignored.
    CHECK_HR ( st.get ( )->Arm ( 0, uWatched ), E_INVALIDARG );
    CHECK_HR ( st.get ( )->Arm ( MSGF_TRIG_UPDATE, 0 ), E_INVALIDARG );

    // Clearing the sink stops delivery even while the registrations remain.
    st.events ( 0 );
    CHECK ( st.fire ( uWatched, MSGF_TRIG_INSERT ) == 1 );
    CHECK ( oCap.m_nCount == 3 );

    wprintf ( L"  sink saw %d notifications, last type=%u\n"
            , oCap.m_nCount, oCap.m_uLastType );

    // A genuine DELETE of an armed node fires the sink on its own -- the one
    // trigger nobody has to ask for.
    Capture oDel;
    const unsigned long long uDoomed = st.at ( L".Stock.SKU-1003" ).pos ( );
    st.events ( &oDel );
    st.arm ( uDoomed, MSGF_TRIG_DELETE );

    st.at ( L".Stock" ).remove ( L"SKU-1003" );
    CHECK ( oDel.m_nCount == 1 );
    CHECK ( oDel.m_uLastType == MSGF_TRIG_DELETE );
    CHECK ( oDel.m_uLastPos == uDoomed );
    wprintf ( L"  deleting SKU-1003 auto-fired the DELETE sink\n" );

    // Clear it BEFORE the sink object dies: nothing here can tell a freed sink
    // from a live one, and the header says whose job that is.
    st.events ( 0 );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    light::InitConsole ( );
    wprintf ( L"=== MgrPersistTest (Light) - the store: save, address, notify ===\n" );

    try
    {
      Library lib;

      Demo_BuildAndInspect ( lib );
      Demo_SaveLoad        ( lib );
      Demo_Addressing      ( lib );
      Demo_Triggers        ( lib );
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
