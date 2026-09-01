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
// WsaStoreTestCom.cpp
//
// The counterpart of ..\DirectExamples\WsaStoreTest: a whole Msgcore
// store serialised, sent between two hubs over loopback TCP, and rebuilt on the
// far side.
//
// THE ONLY HARNESS IN THIS TREE THAT USES BOTH SERVERS, and the one that shows
// why they are two:
//
//      MsgcoreCom   what is IN a message  -- the store, its tree, its types
//      TargetCom    moving it             -- hubs, connections, delivery
//
// Neither knows about the other. The store becomes a byte array and the byte
// array becomes a payload, and that is the whole of the join -- which is
// exactly the relationship Msgcore and TargetCore have one tier down, preserved
// rather than papered over.
//
// THE ROUND TRIP THROUGH A FILE IS NOT LAZINESS. A P2PmsgMgr's heap is already
// one contiguous, relocation-safe image -- that is why Save can be a plain
// block write -- but the accessors for it (P2PmsgHeap_pImage, _pIOmage,
// _CreateIOMAGE) carry no Msgcore_EXT, so they are not exported from the DLL,
// and Msgcore_c.h therefore cannot publish them either. The only exported route
// to those bytes is Save() to a file and read the file. WsaStoreTest hits the
// same wall from C++; this harness hits it one tier further out. Exporting
// those five functions would remove the round trip from both.
//
// Two hubs in ONE PROCESS, each on its own pump thread inside the facade,
// joined by a loopback TCP connection -- the WsaMeshTest mesh.
//
// Exit codes: 0 SUCCESS  1 SETUP  2 ASSERT  3 a check failed / timed out.

#include "../common/P2PBridge.h"

using namespace msgc;

static const unsigned short kPort       = 7841;
static const wchar_t* const kServerAddr = L"MsgStore.Server";
static const wchar_t* const kClientAddr = L"MsgStore.Client";
static const wchar_t* const kTopicStore = L"store";


// ---------------------------------------------------------------------------
// The store the client will ship. Deliberately varied: several declared widths,
// a nested subtree, an attribute and a timestamp, so that what is checked on
// the far side is the SHAPE and the TYPES and not just one number.
// ---------------------------------------------------------------------------
static void BuildStore ( Store& store )
{
    Field root = store.root();

    Field order;
    CHECK ( SUCCEEDED ( root.declare ( L"Order", Var ( 0 ), &order ) ) );
    CHECK ( SUCCEEDED ( order.declare ( L"Customer", Var ( L"Ivyware Pty Ltd" ) ) ) );
    CHECK ( SUCCEEDED ( order.declare ( L"Total",    Var ( 1299.50 ) ) ) );
    CHECK ( SUCCEEDED ( order.declare ( L"Paid",     Var ( false ) ) ) );
    CHECK ( SUCCEEDED ( order.declareTyped ( L"OrderId", Var ( 10045 ),           TYPE_UINT32 ) ) );
    CHECK ( SUCCEEDED ( order.declareTyped ( L"Lines",   Var ( 3 ),               TYPE_UINT08 ) ) );
    CHECK ( SUCCEEDED ( order.declareTyped ( L"Ref",     Var ( (LONGLONG)90071992547409LL ), TYPE_INT64 ) ) );

    Field ship;
    CHECK ( SUCCEEDED ( order.declare ( L"ShipTo", Var ( L"" ), &ship ) ) );
    CHECK ( SUCCEEDED ( ship.declare ( L"City",     Var ( L"Melbourne" ) ) ) );
    CHECK ( SUCCEEDED ( ship.declare ( L"Postcode", Var ( 3000 ) ) ) );
    // U+20AC, so the far side proves the UTF-16 payload survived the wire.
    CHECK ( SUCCEEDED ( ship.declare ( L"Note",     Var ( L"leave at door \x20AC" ) ) ) );

    Attr a;
    CHECK ( SUCCEEDED ( order.child ( L"Total" ).attributes ( a, true ) ) );
    CHECK ( SUCCEEDED ( a.declare ( L"Currency", Var ( L"AUD" ) ) ) );

    CHECK ( SUCCEEDED ( order.touch() ) );
}

// Everything the far side must find. Written once so that "the store arrived"
// is one call on both sides and cannot drift between them.
static bool VerifyStore ( Store& store, DATE dStamp )
{
    bool ok = true;
    #define WANT(expr) do { if (!(expr)) { ok = false; \
        wprintf ( L"  MISMATCH: %s\n", L#expr ); fflush ( stdout ); } } while (0)

    WANT ( store.root().count() == 1 );
    WANT ( store.fieldAt ( L".Order" ).count() == 7 );
    WANT ( store.fieldAt ( L".Order.Customer" ).asText() == L"Ivyware Pty Ltd" );
    WANT ( store.fieldAt ( L".Order.Total" ).asDouble() == 1299.50 );
    WANT ( store.fieldAt ( L".Order.Paid" ).asBool() == false );
    WANT ( store.fieldAt ( L".Order.Paid" ).typeName() == L"BOOL" );

    // The DECLARED WIDTHS, which are the part a naive transport loses.
    WANT ( store.fieldAt ( L".Order.OrderId" ).typeName() == L"UINT32" );
    WANT ( store.fieldAt ( L".Order.OrderId" ).asLong() == 10045 );
    WANT ( store.fieldAt ( L".Order.Lines" ).typeName() == L"UINT08" );
    WANT ( store.fieldAt ( L".Order.Lines" ).asLong() == 3 );
    WANT ( store.fieldAt ( L".Order.Ref" ).typeName() == L"INT64" );
    WANT ( store.fieldAt ( L".Order.Ref" ).asInt64() == 90071992547409LL );

    WANT ( store.fieldAt ( L".Order.ShipTo.City" ).asText() == L"Melbourne" );
    WANT ( store.fieldAt ( L".Order.ShipTo.Postcode" ).asLong() == 3000 );
    WANT ( store.fieldAt ( L".Order.ShipTo.Note" ).asText() == L"leave at door \x20AC" );

    // The parallel tree came too.
    Attr a;
    if ( SUCCEEDED ( store.fieldAt ( L".Order.Total" ).attributes ( a, false ) ) )
        WANT ( a.item ( L"Currency" ).asText() == L"AUD" );
    else
        WANT ( false );

    WANT ( store.fieldAt ( L".Order" ).timestamp() == dStamp );

    #undef WANT
    return ok;
}

static bool ReadWholeFile ( LPCWSTR path, std::vector<BYTE>& out )
{
    out.clear();
    HANDLE h = ::CreateFileW ( path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if ( h == INVALID_HANDLE_VALUE ) return false;

    const DWORD cb = ::GetFileSize ( h, NULL );
    bool bOk = ( cb != INVALID_FILE_SIZE );
    if ( bOk && cb != 0 )
    {
        out.resize ( cb );
        DWORD cbRead = 0;
        bOk = ( ::ReadFile ( h, &out[0], cb, &cbRead, NULL ) != FALSE ) && cbRead == cb;
    }
    ::CloseHandle ( h );
    return bOk;
}

static bool WriteWholeFile ( LPCWSTR path, const std::vector<BYTE>& data )
{
    HANDLE h = ::CreateFileW ( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL );
    if ( h == INVALID_HANDLE_VALUE ) return false;

    DWORD cbWritten = 0;
    const bool bOk = data.empty() ||
        ( ( ::WriteFile ( h, &data[0], (DWORD)data.size(), &cbWritten, NULL ) != FALSE ) &&
          cbWritten == data.size() );
    ::CloseHandle ( h );
    return bOk;
}


int main ( )
{
    InitConsole();

    wprintf ( L"=== WsaStoreTestCom - a Msgcore store shipped between two hubs ===\n" );
    wprintf ( L"Port : %d (127.0.0.1)   apartment: STA   servers: MsgcoreCom + TargetCom\n\n",
              (int)kPort );
    fflush ( stdout );

    Apartment sta;
    if ( !sta.ok() ) return SetupFailure ( L"CoInitializeEx", E_FAIL );

    // ---- tier 1: the content ------------------------------------------------
    Section ( L"1. Build the store (MsgcoreCom)" );

    Store source;
    if ( !source.ok() )
        return SetupFailure ( L"CoCreateInstance(MsgcoreCom.MsgStore)", source.hr() );

    BuildStore ( source );
    const DATE dStamp = source.fieldAt ( L".Order" ).timestamp();
    CHECK ( dStamp != (DATE)0.0 );
    CHECK ( VerifyStore ( source, dStamp ) );
    Note ( L"built: %d bytes of heap, root '%s'", source.size(), source.rootName().c_str() );

    // ---- serialise ----------------------------------------------------------
    Section ( L"2. Serialise -- Save to a file and read the bytes back" );

    const std::wstring sendFile = TempFile ( L"WsaStoreTestCom.send.p2p" );
    const std::wstring recvFile = TempFile ( L"WsaStoreTestCom.recv.p2p" );
    ::DeleteFileW ( sendFile.c_str() );
    ::DeleteFileW ( recvFile.c_str() );

    CHECK ( SUCCEEDED ( source.save ( sendFile.c_str() ) ) );

    std::vector<BYTE> image;
    CHECK ( ReadWholeFile ( sendFile.c_str(), image ) );
    CHECK ( !image.empty() );
    Note ( L"image: %d bytes (via a file, because the in-memory image is not exported)",
           (int)image.size() );

    // ---- tier 2: the transport ----------------------------------------------
    Section ( L"3. Two hubs, one process, loopback TCP (TargetCom)" );

    p2p::Network net;
    if ( !net.ok() )
        return SetupFailure ( L"CoCreateInstance(TargetCom.P2PNetwork)", net.hr() );
    Note ( L"%s", net.versionString().c_str() );

    const LONG cbMax = net.maxPayload();
    CHECK ( cbMax > 0 );
    Note ( L"MaxPayload: %d bytes -- a bigger store needs chunking", cbMax );

    // The cap is real and this harness stays under it deliberately, rather than
    // discovering it at Send time.
    CHECK ( (LONG)image.size() < cbMax );

    Gate              gArrived;
    std::vector<BYTE> received;
    std::wstring      fromWho;
    std::vector<std::wstring> errors;

    p2p::Hub server, client;

    HRESULT hr = net.createHub ( kServerAddr, server );
    if ( FAILED(hr) ) return SetupFailure ( L"CreateHub(server)", hr );

    server.onTopic ( kTopicStore, [&] ( const p2p::Message& m )
    {
        fromWho  = m.source;
        received = m.payload;
        Log ( L"SERVER", L"received %d bytes on topic '%s' from '%s'",
              (int)m.payload.size(), m.topic.c_str(), m.source.c_str() );
        gArrived.bump();
    });
    server.onPeerUp ( [] ( LPCWSTR peer ) { Log ( L"SERVER", L"peer up   : %s", peer ); } );
    server.onError  ( [&] ( LPCWSTR what ) { errors.push_back ( what ? what : L"" );
                                             Log ( L"SERVER", L"error     : %s", what ); } );

    hr = server.listen ( kClientAddr, p2p::TcpListen ( kPort ).c_str() );
    CHECK ( SUCCEEDED ( hr ) );
    if ( FAILED(hr) ) return SetupFailure ( L"Listen", hr );
    Log ( L"SERVER", L"listening for '%s' on port %d", kClientAddr, (int)kPort );

    hr = net.createHub ( kClientAddr, client );
    if ( FAILED(hr) ) return SetupFailure ( L"CreateHub(client)", hr );

    client.onPeerUp ( [&] ( LPCWSTR peer )
    {
        Log ( L"CLIENT", L"peer up   : %s - shipping the store", peer );
        const HRESULT hrSend = client.send ( kServerAddr, kTopicStore,
                                             image.empty() ? NULL : &image[0],
                                             (unsigned int)image.size() );
        Log ( L"CLIENT", L"Send      : %s (%d bytes)", HrName ( hrSend ), (int)image.size() );
    });
    client.onError ( [&] ( LPCWSTR what ) { errors.push_back ( what ? what : L"" );
                                            Log ( L"CLIENT", L"error     : %s", what ); } );

    hr = client.connect ( kServerAddr, p2p::TcpDial ( L"127.0.0.1", kPort ).c_str() );
    CHECK ( SUCCEEDED ( hr ) );
    if ( FAILED(hr) ) return SetupFailure ( L"Connect", hr );
    Log ( L"CLIENT", L"dialling 127.0.0.1:%d (retries until answered)", (int)kPort );

    // THIS PUMPS. Hub events are raised on a dispatch thread inside TargetCom
    // and marshalled into this STA, so a WaitForSingleObject here would hang
    // for ever with the message sitting in the queue.
    Log ( L"MAIN", L"waiting up to 15s for connect + delivery (pumping)..." );
    const bool bArrived = gArrived.wait ( 1, 15000 );

    if ( !bArrived )
    {
        Log ( L"MAIN", L"TIMEOUT - the store never arrived" );
        wprintf ( L"\n%d checks, %d failed. Done (exit=%d).\n",
                  g_nChecks, g_nFailed + 1, EXIT_FAIL );
        fflush ( stdout );
        return EXIT_FAIL;
    }

    CHECK ( fromWho == kClientAddr );
    CHECK ( received.size() == image.size() );
    CHECK ( !received.empty() && received == image );      // byte-identical
    Note ( L"payload arrived byte-identical: %d bytes", (int)received.size() );

    // ---- tier 1 again: rebuild ----------------------------------------------
    Section ( L"4. Rebuild the store on the far side (MsgcoreCom)" );

    CHECK ( WriteWholeFile ( recvFile.c_str(), received ) );

    Store rebuilt;
    CHECK ( rebuilt.ok() );
    CHECK ( SUCCEEDED ( rebuilt.open ( recvFile.c_str() ) ) );

    CHECK ( rebuilt.rootName() == source.rootName() );
    CHECK ( VerifyStore ( rebuilt, dStamp ) );
    Note ( L"rebuilt: %d children, Order.Total=%.2f %s",
           rebuilt.root().count(),
           rebuilt.fieldAt ( L".Order.Total" ).asDouble(),
           L"AUD" );

    // AND IT IS A REAL STORE, not a read-only snapshot: the far side can write
    // to it, and its own P2Pos values are its own.
    CHECK ( SUCCEEDED ( rebuilt.fieldAt ( L".Order.Paid" ).setValue ( Var ( true ) ) ) );
    CHECK ( rebuilt.fieldAt ( L".Order.Paid" ).asBool() == true );
    CHECK ( source.fieldAt ( L".Order.Paid" ).asBool() == false );   // the sender is untouched

    CHECK ( rebuilt.fieldAt ( L".Order" ).p2pos() != 0 );

    // ---- the one report that is NOT a failure -------------------------------
    //
    // Both hubs raised OnError during the handshake, and it is worth reading
    // rather than suppressing. "MsgStore.Server" and "MsgStore.Client" are
    // SIBLINGS in the dotted address tree -- neither is an ancestor of the
    // other -- so the facade warns that nothing can be ROUTED through this edge
    // and a broadcast will not relay beyond it.
    //
    // That is exactly right and exactly harmless here: this harness sends
    // DIRECT traffic over one link, which a sibling edge carries perfectly, as
    // the 2925 bytes above demonstrate. The warning matters when a third hub
    // expects to be reached THROUGH one of these two.
    //
    // It arrives as OnError because the corresponding HRESULT,
    // P2PF_S_UNRELATED_LINK, is a SUCCESS code -- and automation discards those
    // (see MgrCApiTestCom section 6 for the same effect measured on this tier).
    // So the event is the only way a client hears about it at all.
    Section ( L"5. The topology warning -- a success code that had to become an event" );

    int nUnrelated = 0, nReal = 0;
    for ( size_t i = 0; i < errors.size(); ++i )
    {
        if ( errors[i].find ( L"neither ancestor nor descendant" ) != std::wstring::npos )
            ++nUnrelated;
        else
        {
            ++nReal;
            Note ( L"unexpected: %s", errors[i].c_str() );
        }
    }

    CHECK ( nReal == 0 );                   // nothing went actually wrong
    CHECK ( nUnrelated == 2 );              // one per hub, as expected for siblings
    Note ( L"%d sibling-link warnings, %d real errors -- direct traffic is unaffected",
           nUnrelated, nReal );

    ::DeleteFileW ( sendFile.c_str() );
    ::DeleteFileW ( recvFile.c_str() );

    Log ( L"MAIN", L"shutdown begin" );
    return Verdict();
}
