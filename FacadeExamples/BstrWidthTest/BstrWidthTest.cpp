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
// BstrWidthTest.cpp  (Light -- MsgFacade)
//
// THE STORAGE LAYER the other harnesses stand on without naming it: the heap
// under a store, and the ADDRESSING WIDTH that decides how big its internal
// pointers are.
//
// This is the harness where the facade's surface is SMALLER than the
// original's, and deliberately so. ..\DirectExamples\BstrWidthTest
// covered five things; two of them are gone here, and saying which, and why,
// is most of what this file is for:
//
//   original                              light (this file)
//   ---------------------------------------------------------------------
//   P3PmsgBSTR(VBLock_Addr32, 4096)       lib.createStore(MSGF_ADDR_32,4096,0)
//   oBSTR.Init(L"Envelope", data)         st.rename(L"Envelope")
//   oBSTR.r_name() / r_data()             st.rootname() / st.root()
//   P3PmsgBSTR16 / 32 / 64                MSGF_ADDR_16 / _32 / _64
//   oBSTR.Sizeof()                        st.size()
//   P2PmsgMgr16 / 32 / 64                 the same three constants
//   -----------------------------------------------------------------
//   r_item(VBLockBSTR_MSG, bCreate)       NOT EXPOSED -- section 4
//   PageRegistration / PageDatasetIn/Out  NOT EXPOSED -- section 5
//   SafeRegistrationPush                  NOT EXPOSED -- section 5
//   P3PmsgField16                         NOT EXPOSED (it is a P3PmsgField
//                                          with one extra constructor; there
//                                          are no field TYPES here at all)
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS, 1 = SETUP, 3 = at least one check failed.

#include "MsgFacadeFn.hpp"
#include "LightHarness.h"

using namespace msgf;

// The same little tree, whatever the heap underneath it.
static void BuildSmallTree ( Store& st )
{
    Node root = st.root ( );
    root.declareInt  ( L"Alpha", 1 );
    root.declareText ( L"Beta",  L"two" );
    Node gamma = root.declareText ( L"Gamma", L"" );
    gamma.declareReal ( L"Delta", 4.5 );
}


// =========================================================================
// 1. A store IS the heap, plus a named root
// =========================================================================
//
// The original's P3PmsgBSTR was "a heap, a named root item, and a fixed set of
// well-known sections" -- and P2PmsgMgr was that with a file attached.
// TargetCore's P2PeerMsg derives from the same class: an envelope IS one of
// these.
//
// The facade exposes the union of what a CLIENT of a store needs from that: a
// heap it can size and ask about, and a root it can name. What it does not
// expose is the class distinction, because from outside there was never a
// reason to hold one without the other.
//
static void Demo_HeapAndRoot ( Library& lib )
{
    light::Section ( L"1. a store is a heap, plus a named root" );

    Store st = lib.createStore ( MSGF_ADDR_32, 4096, 0 );
    CHECK ( st.ok ( ) );
    CHECK ( st.size ( ) > 0 );

    // Init(name, value) becomes: name the root, then write into it. The root
    // is an ordinary node -- it has a name, children and attributes -- which is
    // exactly what made the original's root a P3PmsgItem.
    st.rename ( L"Envelope" );
    CHECK ( st.rootname ( ) == L"Envelope" );

    Node root = st.root ( );
    CHECK ( root.kind ( ) == MSGF_KIND_ITEM );
    CHECK ( root.path ( ) == L"" );                  // the root's path is empty
    CHECK ( root.name ( ).empty ( ) );               // ...and so is its own name

    // THE ROOT'S NAME IS THE STORE'S, NOT THE NODE'S. GetRootname answers it;
    // IMsgNode::GetName does not, and the header says so. That is the one
    // seam the original did not have, and it exists because a path node's name
    // is the step that reached it -- and nothing reaches the root.

    root.declareText ( L"Verb", L"PUT" );
    root.declareInt  ( L"Seq",  17 );
    CHECK ( root.count ( ) == 2 );
    CHECK ( root.child ( L"Seq" ).asInt ( ) == 17 );

    int bDirty = 0;
    CHECK_HR ( st.get ( )->IsDirty ( &bDirty ), S_OK );
    CHECK ( bDirty != 0 );

    wprintf ( L"  Addr32 store '%s': heap %u bytes, %u children\n"
            , st.rootname ( ).c_str ( ), st.size ( ), root.count ( ) );
}


// =========================================================================
// 2. The addressing width
// =========================================================================
//
// Every offset stored inside a heap image is uAddrNN bits wide. The width is
// chosen at creation and is a property of the HEAP, not of the tree -- the
// same tree costs a different number of bytes at each width, and a narrow heap
// simply cannot address a large one.
//
// The original said this once per class (P3PmsgBSTR16/32/64, then
// P2PmsgMgr16/32/64, six typedefs for one idea). Here it is one argument.
//
static void Demo_Widths ( Library& lib )
{
    light::Section ( L"2. MSGF_ADDR_16 / _32 / _64 -- the width of an offset" );

    Store e16 = lib.createStore ( MSGF_ADDR_16, 0, 0 );
    Store e32 = lib.createStore ( MSGF_ADDR_32, 0, 0 );
    Store e64 = lib.createStore ( MSGF_ADDR_64, 0, 0 );

    CHECK ( e16.ok ( ) );
    CHECK ( e32.ok ( ) );
    CHECK ( e64.ok ( ) );

    // AND HERE IS THE MEASUREMENT THAT SURPRISED THIS HARNESS. The original
    // checked oB16.Sizeof() < oB32.Sizeof() < oB64.Sizeof() on three
    // default-constructed P3PmsgBSTRs, and it held: an empty heap really does
    // cost more at 64 bits than at 16, because every control key in its root
    // header is that many bits wide.
    //
    // Through a STORE it does not hold, and the original saw the same thing
    // one section later without drawing the conclusion: its three P2PmsgMgrs
    // "report the same number here". size() is COMMITTED bytes, and a manager
    // commits its initial request up front -- ~2 KB, which swamps a header
    // difference of a few bytes. So the width is invisible from outside until
    // a tree is big enough to be addressed differently.
    const unsigned int u16 = e16.size ( );
    const unsigned int u32 = e32.size ( );
    const unsigned int u64 = e64.size ( );
    CHECK ( u16 == u32 );
    CHECK ( u32 == u64 );
    wprintf ( L"  empty heaps: 16=%u  32=%u  64=%u bytes (all the initial request)\n"
            , u16, u32, u64 );

    // Identical content, whatever the width.
    Store s16 = lib.createStore ( MSGF_ADDR_16, 2048, 0 );
    Store s32 = lib.createStore ( MSGF_ADDR_32, 2048, 0 );
    Store s64 = lib.createStore ( MSGF_ADDR_64, 2048, 0 );
    BuildSmallTree ( s16 );
    BuildSmallTree ( s32 );
    BuildSmallTree ( s64 );

    CHECK ( s16.root ( ).count ( ) == 3 );
    CHECK ( s32.root ( ).count ( ) == 3 );
    CHECK ( s64.root ( ).count ( ) == 3 );
    CHECK ( s16.root ( ).child ( L"Alpha" ).asInt ( ) == 1 );
    CHECK ( s64.root ( ).child ( L"Alpha" ).asInt ( ) == 1 );
    CHECK ( s32.at ( L".Gamma.Delta" ).asReal ( ) == 4.5 );
    CHECK ( s16.at ( L".Gamma.Delta" ).asReal ( ) == 4.5 );

    // size() is COMMITTED bytes, not requested ones -- the same distinction
    // MgrPersistTest makes -- so all three report their 2 KB request until a
    // tree actually outgrows it. The width shows up in the header above, and
    // in how far each can address, not in a small tree.
    wprintf ( L"  same tree, committed: 16=%u  32=%u  64=%u bytes\n"
            , s16.size ( ), s32.size ( ), s64.size ( ) );

    // An unknown width is refused rather than quietly defaulted.
    HRESULT hrBad = S_OK;
    try                      { lib.createStore ( 99, 0, 0 ); }
    catch ( const Error& e ) { hrBad = e.code ( ); }
    CHECK ( FAILED ( hrBad ) );
    wprintf ( L"  an unknown address width answers %s\n", light::HrName ( hrBad ) );

    // 0 means "the platform default", which is what every other harness in
    // this tree passes.
    Store dflt = lib.createStore ( );
    CHECK ( dflt.ok ( ) );
    BuildSmallTree ( dflt );
    CHECK ( dflt.root ( ).count ( ) == 3 );

    // A TINY INITIAL REQUEST IS RAISED, NOT HONOURED -- and that is the second
    // thing writing this harness measured. An initial size of 1..256 bytes
    // does not fail in the core: it DOES NOT RETURN, at any width. So the
    // facade floors the request at 512, which is the smallest that comes back.
    // Reaching this line at all is most of the check.
    Store floored = lib.createStore ( MSGF_ADDR_64, 16, 0 );
    CHECK ( floored.ok ( ) );
    CHECK ( floored.size ( ) >= 512 );
    BuildSmallTree ( floored );
    CHECK ( floored.root ( ).count ( ) == 3 );
    wprintf ( L"  a 16-byte initial request came back as a %u-byte heap\n"
            , floored.size ( ) );
}


// =========================================================================
// 3. What a width actually caps
// =========================================================================
//
// A 16-bit offset addresses 64 KB, and that is the whole store: tree, names,
// values and free space. So the width is not a hint -- it is a ceiling, and a
// caller that picks one is picking how much it can ever hold.
//
// maxBytes is the OTHER ceiling, and the one a caller sets deliberately. The
// heap grows on demand from initialBytes towards it and no further.
//
static void Demo_Ceiling ( Library& lib )
{
    light::Section ( L"3. what a width caps, and what maxBytes caps" );

    // A 64-bit store with a deliberately small maxBytes. Fill it until the
    // store refuses to grow -- which it does by answering, not by dying.
    Store st = lib.createStore ( MSGF_ADDR_64, 512, 8192 );
    Node  root = st.root ( );

    int     nDeclared = 0;
    HRESULT hrStop    = S_OK;

    for ( int i = 0; i < 5000; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"node%04d", i );
      const HRESULT hr = root.get ( )->DeclareText ( MSGF_SCOPE_CHILD, szName
                                                   , L"a value with some length to it"
                                                   , 0, 0 );
      if ( FAILED ( hr ) ) { hrStop = hr; break; }
      ++nDeclared;
    }

    CHECK ( nDeclared > 0 );
    CHECK ( FAILED ( hrStop ) );                     // it stopped, rather than growing
    CHECK ( hrStop == MSGF_E_CORE );                 // "the core raised or refused"
    CHECK ( st.size ( ) <= 8192 );

    wprintf ( L"  an 8 KB ceiling took %d nodes, then answered %s (heap %u bytes)\n"
            , nDeclared, light::HrName ( hrStop ), st.size ( ) );

    // ...and the store is still SOUND afterwards: a refused declare is not a
    // half-written one, so everything already in it still reads.
    CHECK ( st.ok ( ) );
    CHECK ( root.count ( ) == (unsigned int)nDeclared );
    CHECK ( root.child ( L"node0000" ).asText ( ) == L"a value with some length to it" );
    CHECK ( !root.exists ( L"node4999" ) );

    // A store with NO ceiling (maxBytes 0) passes the same point without
    // noticing it.
    Store big = lib.createStore ( MSGF_ADDR_64, 512, 0 );
    Node  broot = big.root ( );
    for ( int i = 0; i < nDeclared + 200; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"node%04d", i );
      broot.declareText ( szName, L"a value with some length to it" );
    }
    CHECK ( broot.count ( ) == (unsigned int)nDeclared + 200 );
    CHECK ( big.size ( ) > 8192 );
    wprintf ( L"  the same load in an uncapped heap: %u bytes\n", big.size ( ) );
}


// =========================================================================
// 4. The sections, and what replaced them
// =========================================================================
//
// The original's most interesting P3PmsgBSTR feature was its SECTIONS: five
// well-known child items (Net, Sys, Evt, Wrp, Msg) reached by FLAG rather than
// by name -- r_item(VBLockBSTR_MSG, /*bCreate*/ true) -- which is how
// P2PeerMsg keeps its routing headers apart from its application payload.
//
// THE FACADE DOES NOT EXPOSE THEM, and the reason is worth stating: a section
// is an ordinary child item with a reserved name and a numeric alias for that
// name. The alias saves a string compare inside the kernel, where it is on the
// hot path of every message; it buys a CLIENT nothing, and it costs the ABI a
// second way of naming a child that the path grammar could not express.
//
// So the equivalent is: reserved names, spelt out. Which is what this section
// is, and it is the same tree in the same heap.
//
static void Demo_Sections ( Library& lib )
{
    light::Section ( L"4. sections, spelt as reserved names" );

    Store st = lib.createStore ( MSGF_ADDR_32, 4096, 0 );
    st.rename ( L"Envelope" );
    Node root = st.root ( );

    // The two the original created.
    CHECK ( !root.exists ( L"Msg" ) );
    Node msg = root.declareText ( L"Msg", L"" );
    CHECK ( root.exists ( L"Msg" ) );

    CHECK ( !root.exists ( L"Sys" ) );
    root.declareText ( L"Sys", L"" );
    CHECK ( root.exists ( L"Sys" ) );

    // A section is a normal node, so it takes descendants like any other.
    msg.declareText ( L"Verb", L"PUT" );
    msg.declareInt  ( L"Seq",  17 );
    CHECK ( st.at ( L".Msg" ).count ( ) == 2 );
    CHECK ( st.at ( L".Msg.Seq" ).asInt ( ) == 17 );

    // ...and, unlike a flag, a name is addressable from outside: the whole
    // section has a path, so a client can be handed one without being handed
    // the enum that names it.
    CHECK ( st.at ( L".Msg.Verb" ).asText ( ) == L"PUT" );
    CHECK ( st.at ( L".Msg.Verb" ).path ( ) == L".Msg.Verb" );

    wprintf ( L"  '%s' carries %u sections, Msg.Verb='%s'\n"
            , st.rootname ( ).c_str ( ), root.count ( )
            , st.at ( L".Msg.Verb" ).asText ( ).c_str ( ) );
}


// =========================================================================
// 5. The width through Save/Load -- and what is genuinely missing
// =========================================================================
//
// The width is part of the image, so a store written at one width comes back
// at it. That is the property that makes the knob meaningful for something
// going over a wire or onto a small device: the receiver does not have to be
// told.
//
// THIS SECTION USED TO SAY DEMAND PAGING HAD NO FACADE SPELLING AT ALL, and
// that it could not be a simple addition -- the original's fifth section is
// PageRegistration / PageDatasetIn / PageDatasetOut and the SafeRegistrationPush
// RAII bracket around swapping them, a hook that lets the application answer
// "this subtree is not in memory yet". The argument was that a paging callback
// fires from INSIDE the kernel while the store's lock is held and the heap is
// mid-operation, which is the one moment the facade's own contract has nothing
// useful to say.
//
// ABI 2 EXPOSES IT, and resolves that by stating the contract rather than
// pretending it is the ordinary one. Section 6 below is the whole of it, and the
// table is the point: paging is the INVERSE of a change sink at every step.
//
static void Demo_WidthThroughFile ( Library& lib )
{
    light::Section ( L"5. the width survives the file" );

    const std::wstring strFile = light::TempPath ( L"mscs_light_width" );

    const unsigned char aWidths[] = { MSGF_ADDR_16, MSGF_ADDR_32, MSGF_ADDR_64 };
    for ( int i = 0; i < 3; ++i )
    {
      ::DeleteFileW ( strFile.c_str ( ) );

      unsigned int uSaved = 0;
      {
        Store st = lib.createStore ( aWidths[i], 2048, 0 );
        BuildSmallTree ( st );
        st.rename ( L"Widths" );
        st.save ( strFile, MSGF_SAVE_DEFRAGMENT );
        uSaved = st.size ( );
      }

      Store re = lib.openStore ( strFile );
      CHECK ( re.ok ( ) );
      CHECK ( re.size ( ) == uSaved );               // the same heap, byte for byte
      CHECK ( re.rootname ( ) == L"Widths" );
      CHECK ( re.root ( ).count ( ) == 3 );
      CHECK ( re.at ( L".Gamma.Delta" ).asReal ( ) == 4.5 );
      CHECK ( re.at ( L".Beta" ).asText ( ) == L"two" );

      wprintf ( L"  addr=%u : saved %u bytes, reloaded identical\n"
              , (unsigned)aWidths[i], uSaved );
    }

    ::DeleteFileW ( strFile.c_str ( ) );
}


// =========================================================================
// 6. Demand paging -- the one hook whose contract is the inverse of the rest
// =========================================================================
//
// This is what the original's PageRegistration / PageDatasetIn /
// PageDatasetOut / SafeRegistrationPush do, reached through ABI 2. Section 5
// above used to say it was unreachable; it is reachable, and the reason it took
// a second ABI to arrive is that its contract cannot be softened into the shape
// every other callback here has:
//
//                       a change sink            OnPageIn / OnPageOut
//   runs on             a dispatch thread        the ACCESSING thread
//   timing              after the fact           DURING, core is BLOCKED
//   store lock          not held                 HELD
//   return value        ignored                  the answer; false = refused
//   may re-enter        yes, freely              only the named subtree
//   may block           yes                      no
//
// A page-in that has not returned is data that is not there, so there is no
// version of this that defers -- the queueing that makes a change sink safe
// cannot be applied. What the facade adds is that an ABSENT handler answers
// "not handled" rather than "handled", so a half-installed sink fails visibly
// instead of silently swallowing a page-in.
//
static void Demo_Paging ( Library& lib )
{
    light::Section ( L"6. demand paging -- registered, synchronous, answering" );

    Store st = lib.createStore ( MSGF_ADDR_64, 4096, 0 );
    Node  root = st.root ( );
    Node  data = root.declareInt ( L"Dataset", 0 );
    const unsigned long long uPos = data.pos ( );
    CHECK ( uPos != 0 );

    // With NO sink installed, a page request is refused rather than pretended.
    CHECK ( !st.pageIn  ( uPos ) );
    CHECK ( !st.pageOut ( uPos ) );

    int nIn = 0, nOut = 0, nFlush = -1;
    unsigned long long uSeen = 0;
    bool bAnswer = true;

    Paging oSink
    (
      [&] ( unsigned long long pos ) -> bool
      { ++nIn;  uSeen = pos; return bAnswer; },
      [&] ( unsigned long long pos, bool bFlush ) -> bool
      { ++nOut; uSeen = pos; nFlush = bFlush ? 1 : 0; return bAnswer; }
    );

    st.paging ( &oSink );

    CHECK ( st.pageIn ( uPos ) );
    CHECK ( nIn == 1 );
    CHECK ( uSeen == uPos );

    CHECK ( st.pageOut ( uPos, true ) );
    CHECK ( nOut == 1 );
    CHECK ( nFlush == 1 );

    CHECK ( st.pageOut ( uPos, false ) );
    CHECK ( nFlush == 0 );                           // the flag arrives as sent

    // THE RETURN VALUE IS THE ANSWER, not a courtesy. A handler that refuses is
    // reported to whatever provoked the access.
    bAnswer = false;
    CHECK ( !st.pageIn ( uPos ) );
    CHECK ( nIn == 2 );                              // it still RAN
    bAnswer = true;

    // push/pop suspends the registration for a section of work -- what the
    // kernel's SafeRegistrationPush does with a constructor and a destructor. A
    // Save is the usual reason: it walks everything and must not fault the whole
    // store in on the way past.
    st.pushPaging ( );
    CHECK ( !st.pageIn ( uPos ) );                   // suspended: nothing to call
    CHECK ( nIn == 2 );                              // and nothing ran

    st.popPaging ( );
    CHECK ( st.pageIn ( uPos ) );
    CHECK ( nIn == 3 );                              // restored

    // TWO STATES ARE REFUSED, and both are protections rather than pedantry: a
    // SECOND push (there is one slot, so a nested push would lose the first
    // set) and a pop with nothing pushed.
    st.pushPaging ( );
    try   { st.pushPaging ( ); CHECK ( false ); }
    catch ( const Error& e ) { CHECK ( e.code ( ) == MSGF_E_STATE ); }
    st.popPaging ( );
    try   { st.popPaging ( ); CHECK ( false ); }
    catch ( const Error& e ) { CHECK ( e.code ( ) == MSGF_E_STATE ); }

    // Clearing the sink puts it back where it started.
    st.paging ( 0 );
    CHECK ( !st.pageIn ( uPos ) );
    CHECK ( nIn == 3 );

    wprintf ( L"  %d page-ins, %d page-outs, all on the calling thread\n", nIn, nOut );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    light::InitConsole ( );
    wprintf ( L"=== BstrWidthTest (Light) - the heap under a store, and its width ===\n" );

    try
    {
      Library lib;

      Demo_HeapAndRoot     ( lib );
      Demo_Widths          ( lib );
      Demo_Ceiling         ( lib );
      Demo_Sections        ( lib );
      Demo_WidthThroughFile( lib );
      Demo_Paging          ( lib );
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
