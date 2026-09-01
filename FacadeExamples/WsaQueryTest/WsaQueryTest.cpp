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
// WsaQueryTest.cpp  (Light -- MsgFacade + TargetFacade)
//
// A store used as a SERVICE: one hub owns a catalogue, the other queries it by
// PATH over a loopback TCP socket, and each answer carries the value AND its
// type AND the node's position. Two hubs, one process.
//
// WsaStoreTest shipped a whole store as one opaque blob. This is the other
// shape, and the more usual one: the store STAYS on the server and only small
// typed answers cross the wire, which makes the store the authority and the
// messages a query protocol over it.
//
//   original                                 light (this file)
//   ---------------------------------------------------------------------
//   DECLARE_P2PeerMsg_MAP + BEGIN/END map    hub.onTopic(L"StoreQuery", ...)
//   ON_P2PeerMsg(kMsgQuery, On_StoreQuery)   hub.onTopic(L"StoreReply", ...)
//   P2PmsgMgr* m_pStore + delete in ~        msgf::Store, by value
//   m_pStore->RootPath2Object(path)          st.at(path)
//     -- THROWS "Path to object does not       -- throws msgf::Error carrying
//        exist" for a name that is absent         MSGF_E_NO_ITEM, CLIENT-SIDE
//   oField.ToStringType() / ToString()       lib.typeName(n.type()) + a render
//   oField.GetP2Pos()                        n.pos()
//   PostP2PeerMsg(new P2PeerMsg32(...))      hub.sendText(dest, topic, text)
//
// THE PROTOCOL is deliberately trivial, because the interesting part is what
// is behind it rather than the encoding:
//
//   StoreQuery   payload = the query path, a NUL-terminated wide string
//   StoreReply   payload = "path|TYPE|value|pos", same encoding
//
// TWO THINGS THAT GOT SAFER, and both are about the same sentence in the
// original: "A MISS IS AN EXCEPTION, NOT AN EMPTY RESULT."
//
//  1. In the original, RootPath2Object THREW for an unknown path, and
//     converting that throw into an answer was MANDATORY -- not for tidiness,
//     but because an exception escaping a hub handler is caught by the pump,
//     which then DROPS THE CONNECTION. A client asking for a name that did not
//     exist would silently lose the link. Here a miss is MSGF_E_NO_ITEM at the
//     facade boundary, and the only throw is the sugar layer's own, raised and
//     caught inside this file.
//
//  2. The type names agree with themselves. The original's expectations read
//     { L"WSTR16", L"int32", L"int32", L"int32" } -- because ToStringType()
//     spells some types in upper case and others in lower. The facade has ONE
//     vocabulary, both directions (TypeName / TypeFromName), so the reply's
//     type field is a token the client can compare rather than a rendering it
//     has to recognise.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS : every query answered, and every answer matched.
//   1 = SETUP   : the network or a hub could not be created / armed.
//   3 = FAIL    : not all replies arrived in time, or an answer was wrong.

#include "MsgFacadeFn.hpp"
#include "LightMesh.h"

#include <string>

using namespace msgf;

static const unsigned short kTestPort   = 7812;
static const wchar_t* const kServerAddr = L"MsgQuery.Server";
static const wchar_t* const kClientAddr = L"MsgQuery.Client";
static const wchar_t* const kMsgQuery   = L"StoreQuery";
static const wchar_t* const kMsgReply   = L"StoreReply";

// The queries the client will issue, and what each answer must be. Paths are
// in the facade's own grammar -- '.' before a descendant step, '@' before an
// attribute step -- which is the form IMsgNode::GetPath renders, so a client
// can send back something the server handed it earlier.
struct QuerySpec
{
    const wchar_t *lpszPath;
    const wchar_t *lpszType;      // expected type token ("" == expect a miss)
    const wchar_t *lpszValue;     // expected rendered value
};
static const QuerySpec kQueries[] = {
    { L".Product",           L"WSTR16", L"Chartboard" },
    { L".Release.Major",     L"INT32",  L"7"          },
    { L".Release.Minor",     L"INT32",  L"12"         },
    { L".Limits.MaxSeries",  L"INT32",  L"512"        },
    { L".Product@Family",    L"WSTR16", L"analytics"  },   // an ATTRIBUTE
    { L".Missing.Node",      L"",       L""           },   // must MISS cleanly
};
static const int kQueryCount = (int)( sizeof(kQueries) / sizeof(kQueries[0]) );

static volatile LONG g_nAnswered = 0;


// -------------------------------------------------------------------------
// Rendering, which is the one job the facade leaves to the caller.
//
// The kernel had ToString() on every cell; the facade does not, deliberately
// -- a rendering is a presentation decision (how many digits? which locale?
// what does a blob look like?) and a data layer that makes it for you is one
// you have to work around. So a service that wants one writes the three lines
// it actually needs, and the TYPE it sends alongside is what lets the far end
// undo it.
// -------------------------------------------------------------------------
static std::wstring Render ( Library& lib, Node& n )
{
    wchar_t szBuf[64];
    switch ( n.type ( ) )
    {
      case MSGF_TYPE_WSTR16:
      case MSGF_TYPE_BSTR16:
        return n.asText ( );

      case MSGF_TYPE_FLOAT:
      case MSGF_TYPE_DOUBLE:
        ::swprintf_s ( szBuf, L"%g", n.asReal ( ) );
        return szBuf;

      case MSGF_TYPE_GUID:
        return n.asGuid ( );

      case MSGF_TYPE_NULL:
        return std::wstring ( );

      default:
        break;
    }

    // Everything left is the integer family, which one call reads at any
    // width -- the reason this default arm can exist at all.
    long long iVal = 0;
    bool bUnsigned = false;
    try { iVal = n.asInt ( bUnsigned ); }
    catch ( const Error& ) { return lib.typeName ( n.type ( ) ); }

    ::swprintf_s ( szBuf, bUnsigned ? L"%llu" : L"%lld", iVal );
    return szBuf;
}

// Split "a|b|c|d" into its fields.
static void SplitPipe ( const std::wstring& str, std::wstring aOut[], int nMax )
{
    int    n     = 0;
    size_t nFrom = 0;
    while ( n < nMax )
    {
      const size_t nBar = str.find ( L'|', nFrom );
      if ( nBar == std::wstring::npos ) { aOut[n++] = str.substr ( nFrom ); break; }
      aOut[n++] = str.substr ( nFrom, nBar - nFrom );
      nFrom = nBar + 1;
    }
    while ( n < nMax ) aOut[n++].clear ( );
}


// =========================================================================
// The catalogue the server serves
// =========================================================================
//
// Built once, on the MAIN thread, before the hub exists to race with it. The
// store is then touched only from the server's topic handler, so it needs no
// lock of its own -- one hub, one pump, one thread.
//
// (And if that stopped being true, the facade would already be right: it puts
// one critical section around each store and takes it in every method. What
// it cannot make atomic is a SEQUENCE of calls, which is the thing a second
// writer would break.)
//
static void BuildCatalogue ( Store& st )
{
    Node root = st.root ( );

    Node product = root.declareText ( L"Product", L"Chartboard" );
    product.declareText ( L"Family", L"analytics", Attr );

    Node release = root.declareText ( L"Release", L"" );
    release.declareInt ( L"Major", 7 );
    release.declareInt ( L"Minor", 12 );

    Node limits = root.declareText ( L"Limits", L"" );
    limits.declareInt ( L"MaxSeries", 512 );
    limits.declareInt ( L"MaxPoints", 1000000 );
}


// =========================================================================
// SERVER: answer a query out of the store
// =========================================================================
static std::wstring AnswerQuery ( Library& lib, Store& st, const std::wstring& strPath )
{
    try
    {
      Node n = st.at ( strPath );

      // The answer is SELF-DESCRIBING: the type token travels with the value,
      // so the caller never has to guess how to read it, and the position
      // gives it a stable handle to ask again later.
      wchar_t szOut[512];
      ::swprintf_s ( szOut, 512, L"%s|%s|%s|%llu"
                   , strPath.c_str ( )
                   , lib.typeName ( n.type ( ) ).c_str ( )
                   , Render ( lib, n ).c_str ( )
                   , n.pos ( ) );
      return szOut;
    }
    catch ( const Error& e )
    {
      // A miss, a malformed path and a closed store are three different codes,
      // so the reply can say which. The original had one answer for all of
      // them, because it had one exception for all of them.
      light::Log ( L"SERVER", L"  miss: %s -> %s", strPath.c_str ( )
                 , light::HrName ( e.code ( ) ) );
      return strPath + L"|MISS||0";
    }
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    light::InitConsole ( );
    wprintf ( L"=== WsaQueryTest (Light) - querying a store across two hubs ===\n" );
    wprintf ( L"Port : %d (127.0.0.1), %d queries\n\n", (int)kTestPort, kQueryCount );

    light::Gate gDone;

    try
    {
      Library lib;
      p2pf::Network net;

      Store store = lib.createStore ( MSGF_ADDR_64, 4096, 1u << 20 );
      BuildCatalogue ( store );

      // ---- Hub A: SERVER (owns the store) --------------------------------
      p2pf::Hub server = net.createHub ( kServerAddr );

      server.onTopic ( kMsgQuery, [&] ( const p2pf::Message& m )
      {
        const std::wstring strPath  = m.text ( ) ? m.text ( ) : L"";
        const std::wstring strReply = AnswerQuery ( lib, store, strPath );

        wprintf ( L"[SERVER] '%s' -> '%s'\n", strPath.c_str ( ), strReply.c_str ( ) );

        // Reply to whoever asked, on the reply topic.
        server.sendText ( m.source, kMsgReply, strReply.c_str ( ) );
      });
      server.onError ( [] ( const wchar_t *what )
                       { light::Log ( L"SERVER", L"error : %s", what ); } );

      HRESULT hr = server.listen ( kClientAddr, light::TcpListen ( kTestPort ).c_str ( ) );
      if ( FAILED ( hr ) )
      {
        light::Log ( L"SERVER", L"FATAL: listen failed (%s)", light::MeshHrName ( hr ) );
        return light::EXIT_SETUP;
      }
      light::Log ( L"SERVER", L"listening, catalogue loaded" );

      // ---- Hub B: CLIENT --------------------------------------------------
      p2pf::Hub client = net.createHub ( kClientAddr );

      client.onTopic ( kMsgReply, [&] ( const p2pf::Message& m )
      {
        const std::wstring strBody = m.text ( ) ? m.text ( ) : L"";
        std::wstring aPart[4];
        SplitPipe ( strBody, aPart, 4 );

        // Match the answer to the query that asked for it, by path.
        const QuerySpec *pSpec = nullptr;
        for ( int i = 0; i < kQueryCount; ++i )
          if ( aPart[0] == kQueries[i].lpszPath ) { pSpec = &kQueries[i]; break; }

        CHECK ( pSpec != nullptr );
        if ( pSpec )
        {
          if ( pSpec->lpszType[0] == L'\0' )
          {
            CHECK ( aPart[1] == L"MISS" );
            wprintf ( L"[CLIENT] %-22s -> MISS (as expected)\n", aPart[0].c_str ( ) );
          }
          else
          {
            CHECK ( aPart[1] == pSpec->lpszType );
            CHECK ( aPart[2] == pSpec->lpszValue );
            CHECK ( !aPart[3].empty ( ) && aPart[3] != L"0" );   // a real position
            wprintf ( L"[CLIENT] %-22s -> %-7s %-12s @pos %s\n"
                    , aPart[0].c_str ( ), aPart[1].c_str ( )
                    , aPart[2].c_str ( ), aPart[3].c_str ( ) );
          }
        }

        if ( ::InterlockedIncrement ( &g_nAnswered ) >= kQueryCount )
          gDone.open ( );
      });

      // The dialling side speaks first, as always.
      client.onPeerUp ( [&] ( const wchar_t *peer )
      {
        light::Log ( L"CLIENT", L"peer up : %s - issuing the queries", peer );
        for ( int i = 0; i < kQueryCount; ++i )
          client.sendText ( kServerAddr, kMsgQuery, kQueries[i].lpszPath );
      });
      client.onError ( [] ( const wchar_t *what )
                       { light::Log ( L"CLIENT", L"error : %s", what ); } );

      hr = client.connect ( kServerAddr, light::TcpDial ( L"127.0.0.1", kTestPort ).c_str ( ) );
      if ( FAILED ( hr ) )
      {
        light::Log ( L"CLIENT", L"FATAL: connect failed (%s)", light::MeshHrName ( hr ) );
        return light::EXIT_SETUP;
      }
      light::Log ( L"CLIENT", L"dialling 127.0.0.1:%d", (int)kTestPort );

      // ---- Wait ----------------------------------------------------------
      light::Log ( L"MAIN", L"waiting up to 15s for all replies..." );
      if ( !gDone.wait ( 15000 ) )
      {
        ++light::Failed ( );
        light::Log ( L"MAIN", L"TIMEOUT - %ld of %d replies arrived"
                   , (long)g_nAnswered, kQueryCount );
      }
      else if ( light::Failed ( ) == 0 )
        light::Log ( L"MAIN", L"SUCCESS - every query answered correctly from the store" );
      else
        light::Log ( L"MAIN", L"ALL REPLIES ARRIVED but an answer did not match" );

      light::Log ( L"MAIN", L"shutdown begin" );
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
