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
// WsaStoreTest.cpp  (Light -- MsgFacade + TargetFacade)
//
// BOTH KERNELS, BOTH FACADES: a whole Msgcore STORE shipped between two hubs
// over a loopback TCP socket, in one process, and rebuilt on the far side.
//
// This is the harness that shows where the two libraries meet, and the seam is
// the same one the original drew:
//
//     Msgcore     owns the tree and can render it as a flat heap IMAGE
//     Targetcore  moves an opaque byte range from one hub to another
//
// Targetcore has no idea what is in the payload. It carries bytes. Making
// those bytes a Msgcore store is entirely the application's business, and this
// harness is that application -- which is exactly why it is the one file in
// this tree that includes two facade headers and no kernel header at all.
//
//   original (449 lines)                     light (this file)
//   ---------------------------------------------------------------------
//   CWinApp theApp + MFC stdafx              (gone)
//   StartupP2Pmsg(16) + WSAStartup(2,2)      p2pf::Network net;
//   class StoreHub : public P2PeerHub        (gone -- no subclass)
//   On_P2PeerBCast override                  hub.onTopic(L"store", ...)
//   On_ConLoginAck override                  hub.onPeerUp(...)
//   ServiceFactory + PostP2PeerCon           hub.listen(peer, "tcp://:port")
//   ClientFactory + PostP2PeerCon            hub.connect(peer, "tcp://ip:port")
//   Sleep(750) between arm and dial          (unnecessary -- facade dials retry)
//   new P2PeerMsg32(...) + PostP2PeerMsg     hub.send(dest, topic, bytes, size)
//   P2PmsgMgr oMgr; oMgr.Load(scratch)       lib.createStore(); st.load(scratch)
//   oMgr.SelectItem(L"Sensor").c_wstr()      st.at(L".Sensor").asText()
//   catch (P2Pevent*) around the rebuild     catch (const msgf::Error&)
//   CloseHub/Wait/CloseHandle/Cleanup        (destructors, in order)
//
// HOW THE IMAGE IS OBTAINED, unchanged and still the interesting limitation.
// A store's heap is already a single contiguous, relocation-safe image -- that
// is the whole point of the VBLock design, and why Save/Load can be a plain
// block write. But the in-memory accessors for it (P2PmsgHeap_pImage /
// _CreateIOMAGE) carry no Msgcore_EXT, so they are not exported from the
// kernel DLL and neither a client NOR THIS FACADE can reach them. The exported
// route to the same bytes is: save to a file and read it back. The extra file
// round trip is a limitation of the export surface, not of the format, and
// closing it would be a change to Msgcore rather than to MsgFacade.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS : the server rebuilt the store and every field matched.
//   1 = SETUP   : the network or a hub could not be created / armed.
//   3 = FAIL    : nothing delivered in time, or a rebuilt field did not match.

#include "MsgFacadeFn.hpp"
#include "LightMesh.h"

#include <vector>

using namespace msgf;

static const unsigned short kTestPort   = 7811;
static const wchar_t* const kServerAddr = L"MsgStore.Server";
static const wchar_t* const kClientAddr = L"MsgStore.Client";
static const wchar_t* const kTopic      = L"store";


// =========================================================================
// Store <-> bytes: the two halves of one seam
// =========================================================================
//
// Each side owns its OWN scratch file, so nothing is shared behind the
// socket's back -- the bytes really do travel through the mesh.
//
static void BuildStore ( Store& st )
{
    Node root = st.root ( );

    root.declareText ( L"Sensor",   L"thermo-07" );
    root.declareInt  ( L"Interval", 250 );
    root.declareReal ( L"Scale",    0.125 );

    Node limits = root.declareText ( L"Limits", L"" );
    limits.declareInt ( L"Low",  -40 );
    limits.declareInt ( L"High", 125 );

    Vect samples = root.declareVect ( L"Samples", 5, MSGF_TYPE_INT32 );
    for ( unsigned int i = 0; i < 5; ++i )
      samples.setIntAt ( i, (long long)( 200 + i ) );
}

// Serialise: save to a scratch file, then read the file back as bytes.
static bool StoreToBytes ( Store& st, const std::wstring& strScratch
                         , std::vector<unsigned char>& vOut )
{
    vOut.clear ( );
    try { st.save ( strScratch, MSGF_SAVE_DEFRAGMENT ); }
    catch ( const Error& ) { return false; }

    // The save holds the file open on the store, but only against other
    // WRITERS -- the share mode is FILE_SHARE_READ -- so this read of our own
    // live store is legitimate.
    FILE *fp = nullptr;
    if ( ::_wfopen_s ( &fp, strScratch.c_str ( ), L"rb" ) != 0 || !fp )
      return false;

    ::fseek ( fp, 0, SEEK_END );
    const long nSize = ::ftell ( fp );
    ::fseek ( fp, 0, SEEK_SET );
    if ( nSize <= 0 ) { ::fclose ( fp ); return false; }

    vOut.resize ( (size_t)nSize );
    const size_t nRead = ::fread ( &vOut[0], 1, (size_t)nSize, fp );
    ::fclose ( fp );
    return nRead == (size_t)nSize;
}

// Deserialise: lay the received bytes down as a file, then load it.
static bool BytesToStore ( const void *pvData, size_t nSize
                         , const std::wstring& strScratch, Store& st )
{
    FILE *fp = nullptr;
    if ( ::_wfopen_s ( &fp, strScratch.c_str ( ), L"wb" ) != 0 || !fp )
      return false;
    const size_t nWrote = ::fwrite ( pvData, 1, nSize, fp );
    ::fclose ( fp );
    if ( nWrote != nSize ) return false;

    // Load validates the image before trusting it, so a truncated or
    // corrupted payload fails HERE rather than corrupting the heap -- and it
    // fails as MSGF_E_FILE rather than as a throw from inside the heap.
    try { st.load ( strScratch ); }
    catch ( const Error& ) { return false; }
    return true;
}


// =========================================================================
// The two roles
// =========================================================================
//
// The original expressed both as one P2PeerHub subclass with a bool, because
// a hub could only be specialised by deriving from it. Here a role is just
// which callbacks a hub is given, so the two are two functions.
//
static void PostStore ( Library& lib, p2pf::Hub& client )
{
    const std::wstring strScratch = light::TempPath ( L"mscs_light_store_send" );
    ::DeleteFileW ( strScratch.c_str ( ) );

    std::vector<unsigned char> vImage;
    {
      // A 2 KB heap keeps the image comfortably inside one message.
      Store st = lib.createStore ( MSGF_ADDR_64, 2048, 1u << 20 );
      BuildStore ( st );

      if ( !StoreToBytes ( st, strScratch, vImage ) )
      {
        light::Log ( L"CLIENT", L"FATAL: could not serialise the store" );
        return;
      }
    }
    ::DeleteFileW ( strScratch.c_str ( ) );

    if ( vImage.size ( ) >= p2pf::MAX_PAYLOAD )
    {
      light::Log ( L"CLIENT", L"store image %u bytes exceeds MAX_PAYLOAD %u"
                 , (unsigned)vImage.size ( ), p2pf::MAX_PAYLOAD );
      return;
    }

    // The payload is opaque to the messaging side -- a byte range and a
    // length. That is the whole interface between the two libraries.
    const HRESULT hr = client.send ( kServerAddr, kTopic
                                   , &vImage[0], (unsigned int)vImage.size ( ) );
    light::Log ( L"CLIENT", L"sent a %u-byte store image : %s"
               , (unsigned)vImage.size ( ), light::MeshHrName ( hr ) );
}

static void RebuildStore ( Library& lib, const p2pf::Message& m )
{
    light::Log ( L"SERVER", L"received %u bytes from '%s'"
               , m.size, m.source ? m.source : L"<null>" );

    CHECK ( m.payload != nullptr );
    CHECK ( m.size > 0 );
    if ( !m.payload || m.size == 0 ) return;

    const std::wstring strScratch = light::TempPath ( L"mscs_light_store_recv" );
    ::DeleteFileW ( strScratch.c_str ( ) );

    try
    {
      Store st = lib.createStore ( );
      CHECK ( BytesToStore ( m.payload, (size_t)m.size, strScratch, st ) );
      CHECK ( st.ok ( ) );

      Node root = st.root ( );

      // Everything the client put in must come back out, WITH ITS TYPES.
      CHECK ( root.exists ( L"Sensor" ) );
      CHECK ( root.child ( L"Sensor" ).asText ( ) == L"thermo-07" );
      CHECK ( root.child ( L"Interval" ).asInt ( ) == 250 );
      CHECK ( root.child ( L"Scale" ).asReal ( ) == 0.125 );
      CHECK ( root.child ( L"Interval" ).type ( ) == MSGF_TYPE_INT32 );
      CHECK ( root.child ( L"Scale" ).type ( ) == MSGF_TYPE_DOUBLE );

      CHECK ( st.at ( L".Limits.Low" ).asInt ( )  == -40 );
      CHECK ( st.at ( L".Limits.High" ).asInt ( ) == 125 );

      Vect samples = root.vect ( L"Samples" );
      CHECK ( samples.count ( ) == 5 );
      CHECK ( samples.intAt ( 0 ) == 200 );
      CHECK ( samples.intAt ( 4 ) == 204 );

      wprintf ( L"[SERVER] rebuilt store: Sensor='%s' Interval=%lld Scale=%g "
                L"Limits=[%lld,%lld] Samples=%u\n"
              , root.child ( L"Sensor" ).asText ( ).c_str ( )
              , root.child ( L"Interval" ).asInt ( )
              , root.child ( L"Scale" ).asReal ( )
              , st.at ( L".Limits.Low" ).asInt ( )
              , st.at ( L".Limits.High" ).asInt ( )
              , samples.count ( ) );
    }
    catch ( const Error& e )
    {
      // The original had to catch here for TWO reasons, and only one of them
      // survives. The first: a Msgcore failure was a thrown P2Pevent* that had
      // to be Cancel()led or it leaked. The second, and the sharp one: an
      // exception escaping a hub handler was caught by the PUMP, which then
      // DROPPED THE CONNECTION -- so a bad payload silently killed the link.
      //
      // The facade answers HRESULTs, so nothing can escape from the store side
      // at all; this catch is only the sugar layer's msgf::Error, and it is
      // here so that a mismatch is a FAILED CHECK rather than a dropped link.
      ++light::Failed ( );
      wprintf ( L"[SERVER] msgf::Error while rebuilding: %s\n"
              , light::HrName ( e.code ( ) ) );
    }

    ::DeleteFileW ( strScratch.c_str ( ) );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    light::InitConsole ( );
    wprintf ( L"=== WsaStoreTest (Light) - a Msgcore store shipped between two hubs ===\n" );
    wprintf ( L"Port : %d (127.0.0.1)\n\n", (int)kTestPort );

    light::Gate gDone;          // opened when the SERVER has finished checking

    try
    {
      Library lib;              // the DATA facade
      p2pf::Network net;        // the MESSAGING facade: replaces StartupP2Pmsg
                                // + WSAStartup and their teardown on every path

      // ---- Hub A: SERVER ------------------------------------------------
      p2pf::Hub server = net.createHub ( kServerAddr );

      server.onTopic ( kTopic, [&] ( const p2pf::Message& m )
      {
        RebuildStore ( lib, m );
        gDone.open ( );
      });
      server.onError ( [] ( const wchar_t *what )
                       { light::Log ( L"SERVER", L"error : %s", what ); } );

      HRESULT hr = server.listen ( kClientAddr, light::TcpListen ( kTestPort ).c_str ( ) );
      if ( FAILED ( hr ) )
      {
        light::Log ( L"SERVER", L"FATAL: listen failed (%s)", light::MeshHrName ( hr ) );
        return light::EXIT_SETUP;
      }
      light::Log ( L"SERVER", L"listening for '%s' on port %d", kClientAddr, (int)kTestPort );

      // ---- Hub B: CLIENT ------------------------------------------------
      p2pf::Hub client = net.createHub ( kClientAddr );

      // THE DIALLING SIDE MUST SPEAK FIRST. The listening hub's own login leg
      // completes at a different instant, so a message posted from the
      // listener's onPeerUp races the handshake and the far end kills the
      // connection. The original watched the same milestone through
      // On_ConLoginAck.
      client.onPeerUp ( [&] ( const wchar_t *peer )
      {
        light::Log ( L"CLIENT", L"peer up : %s - serialising the store", peer );
        PostStore ( lib, client );
      });
      client.onError ( [] ( const wchar_t *what )
                       { light::Log ( L"CLIENT", L"error : %s", what ); } );

      hr = client.connect ( kServerAddr, light::TcpDial ( L"127.0.0.1", kTestPort ).c_str ( ) );
      if ( FAILED ( hr ) )
      {
        light::Log ( L"CLIENT", L"FATAL: connect failed (%s)", light::MeshHrName ( hr ) );
        return light::EXIT_SETUP;
      }
      light::Log ( L"CLIENT", L"dialling 127.0.0.1:%d (retries until answered)", (int)kTestPort );

      // ---- Wait ----------------------------------------------------------
      light::Log ( L"MAIN", L"waiting up to 15s for the store to arrive..." );
      if ( !gDone.wait ( 15000 ) )
      {
        ++light::Failed ( );
        light::Log ( L"MAIN", L"TIMEOUT - nothing delivered" );
      }
      else if ( light::Failed ( ) == 0 )
        light::Log ( L"MAIN", L"SUCCESS - store rebuilt intact on the far hub" );
      else
        light::Log ( L"MAIN", L"DELIVERED but the rebuilt store did not match" );

      light::Log ( L"MAIN", L"shutdown begin" );
      // ~Hub closes each pump; ~Network shuts the kernel down. In order.
    }
    catch ( const Error& e )
    {
      ++light::Failed ( );
      wprintf ( L"\nUNEXPECTED msgf::Error: %s\n", light::HrName ( e.code ( ) ) );
    }
    catch ( const std::exception& e )
    {
      ::printf ( "FATAL: %s\n", e.what ( ) );
      return light::EXIT_SETUP;
    }

    return light::Verdict ( );
}
