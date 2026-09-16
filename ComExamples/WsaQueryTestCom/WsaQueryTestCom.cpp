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
// WsaQueryTestCom.cpp
//
// The counterpart of ..\DirectExamples\WsaQueryTest, and the other
// half of the pair with WsaStoreTestCom: there the whole store MOVES, here it
// STAYS PUT and is queried across the mesh a field at a time.
//
// Which is the shape most systems actually want. A store is a document and a
// document does not fit in a message -- MaxPayload is 24 KB and a real one is
// megabytes -- so the useful pattern is a lookup service: one hub owns the
// store, the other asks it questions.
//
// THREE THINGS THIS HARNESS EXISTS TO PIN:
//
//  1. THE ANSWER TO A FAILED LOOKUP MUST BE AN ANSWER. WsaQueryTest's original
//     finding was that RootPath2Object reports a missing path by THROWING, and
//     that an exception escaping a hub handler makes the pump drop the
//     connection (P2Pwin32.cpp:3279) -- so the handler must convert it into a
//     reply. At this tier the throw is already gone: MsgcoreCom catches at
//     every entry point and FieldAt answers msgcNoField. The rule survives in a
//     different form, and section 3 states it.
//
//  2. THE CORRELATION TAG. The kernel carries a per-message tag beside the
//     payload, so a reply can be matched to its request without either of them
//     spending a byte of payload on saying which. SendEx/MsgTag are how this
//     tier reaches it. Section 2.
//
//  3. NO BEGIN_P2PeerMsg_MAP, AND THEREFORE NO LNK1194. The C++ harness cannot
//     delay-load Targetcore.dll, because BEGIN_P2PeerMsg_MAP imports the DATA
//     symbol P2PeerHub::P2PeerMsgMap and the linker refuses /DELAYLOAD on a DLL
//     an image imports data from. This program links neither DLL at all -- both
//     servers are found in the registry -- so the constraint simply does not
//     arise. Recorded because it is the clearest single benefit of this tier.
//
// Exit codes: 0 SUCCESS  1 SETUP  2 ASSERT  3 a check failed / timed out.

#include "../common/P2PBridge.h"

using namespace msgc;

static const unsigned short kPort        = 7842;
static const wchar_t* const kServerAddr  = L"MsgQuery.Server";
static const wchar_t* const kClientAddr  = L"MsgQuery.Client";
static const wchar_t* const kTopicAsk    = L"ask";
static const wchar_t* const kTopicAnswer = L"answer";

// The wire form, kept deliberately trivial: a request is a path, a reply is
// "OK<tab>value" or "ERR<tab>0x........". Msgcore itself is not used to encode
// the message -- that is what WsaStoreTestCom does -- because the point here is
// the LOOKUP, and a hand-rolled two-field reply keeps that in view.
static std::wstring MakeOk  ( const std::wstring& v ) { return L"OK\t" + v; }
static std::wstring MakeErr ( HRESULT hr )
{
    WCHAR wsz[64];
    ::swprintf_s ( wsz, L"ERR\t0x%08lX", (unsigned long)hr );
    return wsz;
}

struct Reply
{
    LONG         tag;
    std::wstring body;
};


int main ( )
{
    InitConsole();

    wprintf ( L"=== WsaQueryTestCom - querying a Msgcore store across two hubs ===\n" );
    wprintf ( L"Port : %d (127.0.0.1)   apartment: STA   servers: MsgcoreCom + TargetCom\n\n",
              (int)kPort );
    fflush ( stdout );

    Apartment sta;
    if ( !sta.ok() ) return SetupFailure ( L"CoInitializeEx", E_FAIL );

    // ---- the store, which never leaves this hub -----------------------------
    Section ( L"1. One hub owns a store; the other will only ever ask about it" );

    Store store;
    if ( !store.ok() )
        return SetupFailure ( L"CoCreateInstance(MsgcoreCom.MsgStore)", store.hr() );

    Field root = store.root();
    Field cfg;
    CHECK ( SUCCEEDED ( root.declare ( L"config", Var ( 0 ), &cfg ) ) );
    CHECK ( SUCCEEDED ( cfg.declare ( L"width",  Var ( 1024 ) ) ) );
    CHECK ( SUCCEEDED ( cfg.declare ( L"height", Var ( 768 ) ) ) );
    CHECK ( SUCCEEDED ( cfg.declare ( L"title",  Var ( L"Ivyware \x20AC Chartboard" ) ) ) );

    Field win;
    CHECK ( SUCCEEDED ( cfg.declare ( L"window", Var ( 0 ), &win ) ) );
    CHECK ( SUCCEEDED ( win.declare ( L"x", Var ( 100 ) ) ) );
    CHECK ( SUCCEEDED ( win.declare ( L"y", Var ( 200 ) ) ) );

    CHECK ( store.fieldAt ( L".config.window.y" ).asLong() == 200 );
    Note ( L"store: %d bytes, config has %d children", store.size(), cfg.count() );

    // ---- the mesh -----------------------------------------------------------
    Section ( L"2. Two hubs, loopback TCP, request and reply correlated by tag" );

    p2p::Network net;
    if ( !net.ok() )
        return SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );
    Note ( L"%s", net.versionString().c_str() );

    p2p::Hub server, client;
    std::vector<std::wstring> errors;

    HRESULT hr = net.createHub ( kServerAddr, server );
    if ( FAILED(hr) ) return SetupFailure ( L"CreateHub(server)", hr );

    int nAnswered = 0;

    // THE LOOKUP SERVICE. This runs on the dispatch thread of the server hub,
    // marshalled into this apartment -- so it may take as long as it likes and
    // may call into the store freely, which is the whole reason MsgcoreCom
    // queues its own events rather than calling back from the mutating thread.
    server.onTopic ( kTopicAsk, [&] ( const p2p::Message& m )
    {
        const std::wstring path = m.text();

        // THE TAG. Read from the hub DURING the OnMessage event -- it is valid
        // there and answers p2pfNoMessage anywhere else -- so the reply can be
        // matched to this request without the payload carrying an id.
        LONG tag = 0;
        if ( server.raw() != NULL ) server.raw()->get_MsgTag ( &tag );

        // The lookup. A path that does not resolve is an ANSWER, not a throw
        // and not silence: msgcNoField comes back as an HRESULT and is
        // forwarded to the asker as an ERR reply.
        Field f;
        const HRESULT hrLookup = store.fieldAt ( path.c_str(), f );

        std::wstring body;
        if ( SUCCEEDED ( hrLookup ) && f.ok() )
            body = MakeOk ( f.text() );
        else
            body = MakeErr ( hrLookup );

        Log ( L"SERVER", L"ask tag=%ld '%s' -> %s", tag, path.c_str(),
              SUCCEEDED ( hrLookup ) ? L"OK" : HrName ( hrLookup ) );

        VARIANT v; ::VariantInit ( &v );
        v.vt      = VT_BSTR;
        v.bstrVal = ::SysAllocString ( body.c_str() );

        VARIANT_BOOL vbDummy = VARIANT_FALSE;
        (void)vbDummy;

        // SendEx carries the tag back, so the client does not have to guess.
        if ( server.raw() != NULL )
            server.raw()->SendEx ( Bstr ( kClientAddr ), Bstr ( kTopicAnswer ), v,
                                   tag, -1, 0 );
        ::VariantClear ( &v );

        ++nAnswered;
    });
    server.onPeerUp ( [] ( LPCWSTR peer ) { Log ( L"SERVER", L"peer up   : %s", peer ); } );
    server.onError  ( [&] ( LPCWSTR w ) { errors.push_back ( w ? w : L"" ); } );

    hr = server.listen ( kClientAddr, p2p::TcpListen ( kPort ).c_str() );
    CHECK ( SUCCEEDED ( hr ) );
    if ( FAILED(hr) ) return SetupFailure ( L"Listen", hr );

    hr = net.createHub ( kClientAddr, client );
    if ( FAILED(hr) ) return SetupFailure ( L"CreateHub(client)", hr );

    Gate               gReplies;
    std::vector<Reply> replies;

    client.onTopic ( kTopicAnswer, [&] ( const p2p::Message& m )
    {
        LONG tag = 0;
        if ( client.raw() != NULL ) client.raw()->get_MsgTag ( &tag );

        Reply r;
        r.tag  = tag;
        r.body = m.text();
        replies.push_back ( r );
        Log ( L"CLIENT", L"answer tag=%ld : %s", tag, r.body.c_str() );
        gReplies.bump();
    });

    // The five questions, four answerable and one not -- because "what happens
    // to a bad request" is the part of a lookup service that is worth testing.
    struct { LPCWSTR path; LPCWSTR expect; bool ok; } asks[] = {
        { L".config.width",     L"1024",                  true  },
        { L".config.height",    L"768",                   true  },
        { L".config.title",     L"Ivyware \x20AC Chartboard", true },
        { L".config.window.y",  L"200",                   true  },
        { L".config.depth",     NULL,                     false },
    };
    const int kAsks = _countof(asks);

    client.onPeerUp ( [&] ( LPCWSTR peer )
    {
        Log ( L"CLIENT", L"peer up   : %s - asking %d questions", peer, kAsks );
        for ( int i = 0; i < kAsks; ++i )
        {
            VARIANT v; ::VariantInit ( &v );
            v.vt      = VT_BSTR;
            v.bstrVal = ::SysAllocString ( asks[i].path );

            // Tag 1000+i, so a reply identifies its own question.
            if ( client.raw() != NULL )
                client.raw()->SendEx ( Bstr ( kServerAddr ), Bstr ( kTopicAsk ), v,
                                       1000 + i, -1, 0 );
            ::VariantClear ( &v );
        }
    });
    client.onError ( [&] ( LPCWSTR w ) { errors.push_back ( w ? w : L"" ); } );

    hr = client.connect ( kServerAddr, p2p::TcpDial ( L"127.0.0.1", kPort ).c_str() );
    CHECK ( SUCCEEDED ( hr ) );
    if ( FAILED(hr) ) return SetupFailure ( L"Connect", hr );
    Log ( L"CLIENT", L"dialling 127.0.0.1:%d", (int)kPort );

    // Pumping, as always in an STA.
    Log ( L"MAIN", L"waiting up to 15s for %d answers (pumping)...", kAsks );
    const bool bAll = gReplies.wait ( kAsks, 15000 );

    if ( !bAll )
    {
        Log ( L"MAIN", L"TIMEOUT - only %d of %d answers arrived",
              (int)replies.size(), kAsks );
        wprintf ( L"\n%d checks, %d failed. Done (exit=%d).\n",
                  g_nChecks, g_nFailed + 1, EXIT_FAIL );
        fflush ( stdout );
        return EXIT_FAIL;
    }

    CHECK ( nAnswered == kAsks );
    CHECK ( (int)replies.size() == kAsks );

    // ---- what came back -----------------------------------------------------
    Section ( L"3. The answers, matched by tag" );

    for ( int i = 0; i < kAsks; ++i )
    {
        const LONG wantTag = 1000 + i;

        const Reply *pFound = NULL;
        for ( size_t j = 0; j < replies.size(); ++j )
            if ( replies[j].tag == wantTag ) { pFound = &replies[j]; break; }

        CHECK ( pFound != NULL );
        if ( pFound == NULL ) continue;

        // THE TAG SURVIVED THE ROUND TRIP, which is the claim: neither request
        // nor reply spent a byte of payload saying which question it was about.
        CHECK ( pFound->tag == wantTag );

        if ( asks[i].ok )
        {
            CHECK ( pFound->body == MakeOk ( asks[i].expect ) );
        }
        else
        {
            // A MISSING PATH CAME BACK AS AN ANSWER. Against the C++ API this
            // is where RootPath2Object throws, and an exception escaping a hub
            // handler makes the pump drop the connection -- so the handler had
            // to catch it INSIDE the handler and turn it into a reply. Here
            // FieldAt answers msgcNoField and there was never a throw to
            // escape, so the connection is provably still up afterwards.
            CHECK ( pFound->body == MakeErr ( E_NO_FIELD ) );
            Note ( L"'%s' -> %s (an answer, not a dropped connection)",
                   asks[i].path, pFound->body.c_str() );
        }
    }

    // The link survived the bad request -- which is the real assertion behind
    // finding 1 at the top of this file.
    CHECK ( client.isPeerUp ( kServerAddr ) );
    CHECK ( server.isPeerUp ( kClientAddr ) );
    Note ( L"both peers still up after the failed lookup" );

    // ---- the store is untouched, and still writable -------------------------
    Section ( L"4. The store never moved" );

    CHECK ( store.isValid() );
    CHECK ( store.fieldAt ( L".config" ).count() == 4 );
    CHECK ( store.fieldAt ( L".config.width" ).asLong() == 1024 );

    // A query service is a READER; the owner can still write. Serving five
    // lookups changed nothing about the store's own shape.
    CHECK ( SUCCEEDED ( store.fieldAt ( L".config.width" ).setValue ( Var ( 1920 ) ) ) );
    CHECK ( store.fieldAt ( L".config.width" ).asLong() == 1920 );
    CHECK ( store.filename().empty() );             // never saved: it stayed in memory

    // The sibling-address topology warnings, as in WsaStoreTestCom section 5:
    // expected, harmless for direct traffic, and the only way a client hears
    // about a success code that automation discards.
    int nUnrelated = 0, nReal = 0;
    for ( size_t i = 0; i < errors.size(); ++i )
    {
        if ( errors[i].find ( L"neither ancestor nor descendant" ) != std::wstring::npos )
            ++nUnrelated;
        else { ++nReal; Note ( L"unexpected: %s", errors[i].c_str() ); }
    }
    CHECK ( nReal == 0 );
    Note ( L"%d sibling-link warnings, %d real errors", nUnrelated, nReal );

    Log ( L"MAIN", L"shutdown begin" );
    return Verdict();
}
