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
// P2PBridge.h -- the TargetCom half of the two networked harnesses.
//
// Only WsaStoreTestCom and WsaQueryTestCom include this. They are the two
// harnesses that need BOTH servers at once -- MsgcoreCom for what is in a
// message and TargetCom for moving it -- and they are the only place in this
// tree where the two tiers meet.
//
// This is a deliberately small slice of what _Targetcore_UseExamples\ComExamples\common\
// ComHarness.h wraps: a network, a hub, a topic sink. It is duplicated rather
// than shared across the two trees because a tree of examples that cannot be
// built without a sibling tree of examples is not an example of anything.
//
// The mesh is the WsaMeshTest one: two P2PeerHubs in ONE PROCESS, each on its
// own pump thread inside the facade, joined by a loopback TCP connection. For
// anything about the mesh itself -- hubs vs pumps, thread affinity, the login
// handshake, what happens to an exception in a handler -- read
// ..\..\..\_Targetcore_UseExamples\ArchitectureFAQ.md; none of it is re-explained here.

#pragma once

#include "ComHarness.h"

// TargetCom's generated header and GUIDs. The include path comes from
// HarnessCom.props ($(TargetComGen)), which is that server's MIDL output.
#include "TargetCom_h.h"
#ifndef COMHARNESS_NO_GUIDS
  #include "TargetCom_i.c"
#endif

namespace p2p {

// The facade HRESULTs the two harnesses actually test against.
const HRESULT S_UNRELATED_LINK = MAKE_HRESULT(0, FACILITY_ITF, 0x020C);
const HRESULT E_CLOSED         = MAKE_HRESULT(1, FACILITY_ITF, 0x0206);

inline std::wstring TcpListen ( unsigned short port )
{
    // A listen must NOT name a host: the kernel binds INADDR_ANY regardless.
    return L"tcp://:" + std::to_wstring ( (unsigned)port );
}
inline std::wstring TcpDial ( LPCWSTR host, unsigned short port )
{
    return std::wstring ( L"tcp://" ) + host + L":" + std::to_wstring ( (unsigned)port );
}

// What a topic handler receives, with the payload already unpacked out of its
// VARIANT-wrapped SAFEARRAY.
struct Message
{
    std::wstring      source;
    std::wstring      topic;
    std::vector<BYTE> payload;
    bool              broadcast;

    // The payload seen as a wide string, for the harnesses that send text.
    std::wstring text ( ) const
    {
        if ( payload.size() < sizeof(wchar_t) ) return std::wstring();
        const wchar_t *p = (const wchar_t*)&payload[0];
        const size_t   n = payload.size() / sizeof(wchar_t);
        // SendText includes the terminator; trim it if it is there.
        return std::wstring ( p, ( n > 0 && p[n-1] == L'\0' ) ? n - 1 : n );
    }
};

typedef std::function<void(const Message&)>      MessageHandler;
typedef std::function<void(const wchar_t *peer)> PeerHandler;
typedef std::function<void(const wchar_t *what)> ErrorHandler;

// ---------------------------------------------------------------------------
// A hand-written IDispatch over the _IP2PHubEvents dispinterface. This is the
// code an early-bound client has to write and a scripting client gets for free
// from its host.
// ---------------------------------------------------------------------------
class HubSink : public IDispatch
{
  public:
    HubSink ( ) : m_lRef ( 1 ) { }

    STDMETHOD(QueryInterface) ( REFIID riid, void **ppv )
    {
        if ( !ppv ) return E_POINTER;
        if ( ::IsEqualIID ( riid, IID_IUnknown ) ||
             ::IsEqualIID ( riid, IID_IDispatch ) ||
             ::IsEqualIID ( riid, DIID__IP2PHubEvents ) )
        {
            *ppv = static_cast<IDispatch*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef) ( ) { return ::InterlockedIncrement ( &m_lRef ); }
    STDMETHOD_(ULONG, Release) ( )
    {
        LONG l = ::InterlockedDecrement ( &m_lRef );
        if ( l == 0 ) delete this;
        return l;
    }

    STDMETHOD(GetTypeInfoCount) ( UINT *p ) { if (p) *p = 0; return S_OK; }
    STDMETHOD(GetTypeInfo)      ( UINT, LCID, ITypeInfo** ) { return E_NOTIMPL; }
    STDMETHOD(GetIDsOfNames)    ( REFIID, LPOLESTR*, UINT, LCID, DISPID* ) { return E_NOTIMPL; }

    STDMETHOD(Invoke) ( DISPID dispid, REFIID, LCID, WORD, DISPPARAMS *pdp,
                        VARIANT*, EXCEPINFO*, UINT* )
    {
        if ( !pdp ) return E_POINTER;

        switch ( dispid )
        {
          case 1:       // OnMessage(source, topic, payload, broadcast) -- reversed
          {
            if ( pdp->cArgs != 4 ) return DISP_E_BADPARAMCOUNT;

            Message m;
            m.source    = ( pdp->rgvarg[3].bstrVal ? pdp->rgvarg[3].bstrVal : L"" );
            m.topic     = ( pdp->rgvarg[2].bstrVal ? pdp->rgvarg[2].bstrVal : L"" );
            m.broadcast = ( pdp->rgvarg[0].boolVal != VARIANT_FALSE );
            Unpack ( pdp->rgvarg[1], m.payload );

            std::map<std::wstring, MessageHandler>::iterator it = topics.find ( m.topic );
            if ( it != topics.end() && it->second ) it->second ( m );
            else if ( fallback )                    fallback ( m );
            break;
          }
          case 2: if ( peerUp )   peerUp   ( Arg0 ( pdp ) ); break;
          case 3: if ( peerDown ) peerDown ( Arg0 ( pdp ) ); break;
          case 4: if ( error )    error    ( Arg0 ( pdp ) ); break;
          case 5: break;                            // OnEvent -- OnError covers it here
          case 6: break;                            // OnTimer -- unused
          default: return DISP_E_MEMBERNOTFOUND;
        }
        return S_OK;
    }

    std::map<std::wstring, MessageHandler> topics;
    MessageHandler fallback;
    PeerHandler    peerUp, peerDown;
    ErrorHandler   error;

  private:
    static LPCWSTR Arg0 ( DISPPARAMS *pdp )
    {
        return ( pdp->cArgs >= 1 && pdp->rgvarg[0].bstrVal ) ? pdp->rgvarg[0].bstrVal : L"";
    }

    static void Unpack ( const VARIANT& v, std::vector<BYTE>& out )
    {
        out.clear();
        if ( ( v.vt & VT_ARRAY ) == 0 || v.parray == NULL ) return;

        LONG lo = 0, hi = -1;
        ::SafeArrayGetLBound ( v.parray, 1, &lo );
        ::SafeArrayGetUBound ( v.parray, 1, &hi );
        if ( hi < lo ) return;

        void *p = NULL;
        if ( SUCCEEDED ( ::SafeArrayAccessData ( v.parray, &p ) ) )
        {
            const BYTE *b = (const BYTE*)p;
            out.assign ( b, b + ( hi - lo + 1 ) );
            ::SafeArrayUnaccessData ( v.parray );
        }
    }

    LONG m_lRef;
};

// Wrap raw bytes as the VARIANT(SAFEARRAY of VT_UI1) that Send/Broadcast take.
inline HRESULT MakeBytes ( const void *data, unsigned int cb, VARIANT *pv )
{
    ::VariantInit ( pv );
    SAFEARRAY *psa = ::SafeArrayCreateVector ( VT_UI1, 0, cb );
    if ( !psa ) return E_OUTOFMEMORY;
    if ( cb )
    {
        void *dst = NULL;
        HRESULT hr = ::SafeArrayAccessData ( psa, &dst );
        if ( FAILED(hr) ) { ::SafeArrayDestroy ( psa ); return hr; }
        ::memcpy ( dst, data, cb );
        ::SafeArrayUnaccessData ( psa );
    }
    pv->vt     = VT_ARRAY | VT_UI1;
    pv->parray = psa;
    return S_OK;
}

// ---------------------------------------------------------------------------
// Hub -- IP2PHubCom plus its connection point.
// ---------------------------------------------------------------------------
class Hub
{
  public:
    Hub ( ) : m_pHub ( NULL ), m_pCP ( NULL ), m_pSink ( NULL ), m_dwCookie ( 0 ) { }
   ~Hub ( ) { reset(); }

    Hub             ( const Hub& ) = delete;
    Hub& operator = ( const Hub& ) = delete;

    HRESULT attach ( IP2PHubCom *pHub )
    {
        reset();
        if ( !pHub ) return E_POINTER;
        m_pHub = pHub;                              // takes the caller's reference

        IConnectionPointContainer *pCPC = NULL;
        HRESULT hr = m_pHub->QueryInterface ( IID_IConnectionPointContainer, (void**)&pCPC );
        if ( FAILED(hr) ) return hr;

        hr = pCPC->FindConnectionPoint ( DIID__IP2PHubEvents, &m_pCP );
        pCPC->Release();
        if ( FAILED(hr) ) return hr;

        m_pSink = new HubSink();
        return m_pCP->Advise ( m_pSink, &m_dwCookie );
    }

    bool ok ( ) const { return m_pHub != NULL && m_dwCookie != 0; }

    Hub& onTopic   ( const std::wstring& t, MessageHandler h ) { if (m_pSink) m_pSink->topics[t] = h; return *this; }
    Hub& onPeerUp  ( PeerHandler h )  { if (m_pSink) m_pSink->peerUp   = h; return *this; }
    Hub& onPeerDown( PeerHandler h )  { if (m_pSink) m_pSink->peerDown = h; return *this; }
    Hub& onError   ( ErrorHandler h ) { if (m_pSink) m_pSink->error    = h; return *this; }

    HRESULT listen  ( LPCWSTR toPeer, LPCWSTR endpoint )
    { return m_pHub ? m_pHub->Listen  ( msgc::Bstr(toPeer), msgc::Bstr(endpoint) ) : E_POINTER; }
    HRESULT connect ( LPCWSTR toPeer, LPCWSTR endpoint )
    { return m_pHub ? m_pHub->Connect ( msgc::Bstr(toPeer), msgc::Bstr(endpoint) ) : E_POINTER; }

    HRESULT sendText ( LPCWSTR dest, LPCWSTR topic, LPCWSTR text )
    { return m_pHub ? m_pHub->SendText ( msgc::Bstr(dest), msgc::Bstr(topic), msgc::Bstr(text) ) : E_POINTER; }

    HRESULT send ( LPCWSTR dest, LPCWSTR topic, const void *data, unsigned int cb )
    {
        if ( !m_pHub ) return E_POINTER;
        VARIANT v;
        HRESULT hr = MakeBytes ( data, cb, &v );
        if ( FAILED(hr) ) return hr;
        hr = m_pHub->Send ( msgc::Bstr(dest), msgc::Bstr(topic), v );
        ::VariantClear ( &v );
        return hr;
    }

    bool isPeerUp ( LPCWSTR peer ) const
    {
        if ( !m_pHub ) return false;
        VARIANT_BOOL vb = VARIANT_FALSE;
        return SUCCEEDED ( m_pHub->IsPeerUp ( msgc::Bstr(peer), &vb ) ) && vb != VARIANT_FALSE;
    }

    LONG maxPayloadSeen ( ) const { return m_lMaxPayload; }
    void setMaxPayload ( LONG v ) { m_lMaxPayload = v; }

    IP2PHubCom* raw ( ) const { return m_pHub; }

  private:
    void reset ( )
    {
        if ( m_pCP && m_dwCookie ) { m_pCP->Unadvise ( m_dwCookie ); m_dwCookie = 0; }
        if ( m_pSink ) { m_pSink->Release(); m_pSink = NULL; }
        if ( m_pCP )   { m_pCP->Release();   m_pCP = NULL; }
        if ( m_pHub )  { m_pHub->Close(); m_pHub->Release(); m_pHub = NULL; }
    }

    IP2PHubCom       *m_pHub;
    IConnectionPoint *m_pCP;
    HubSink          *m_pSink;
    DWORD             m_dwCookie;
    LONG              m_lMaxPayload = 0;
};

// ---------------------------------------------------------------------------
// Network -- CoCreateInstance(TargetCom.P2PNetwork) and the hub factory.
// ---------------------------------------------------------------------------
class Network
{
  public:
    Network ( )
    {
        m_hr = ::CoCreateInstance ( CLSID_P2PNetwork, NULL, CLSCTX_INPROC_SERVER,
                                    IID_IP2PNetworkCom, (void**)m_sp.addr() );
    }

    bool    ok ( ) const { return SUCCEEDED(m_hr) && m_sp.ok(); }
    HRESULT hr ( ) const { return m_hr; }

    HRESULT createHub ( LPCWSTR address, Hub& hub )
    {
        if ( !m_sp.ok() ) return E_POINTER;
        IP2PHubCom *p = NULL;
        HRESULT h = m_sp->CreateHub ( msgc::Bstr(address), &p );
        if ( FAILED(h) ) return h;
        return hub.attach ( p );
    }

    std::wstring versionString ( ) const
    {
        if ( !m_sp.ok() ) return std::wstring();
        BSTR bs = NULL;
        m_sp->get_VersionString ( &bs );
        return msgc::Take ( bs );
    }

    LONG maxPayload ( ) const
    {
        if ( !m_sp.ok() ) return 0;
        LONG v = 0;
        m_sp->get_MaxPayload ( &v );
        return v;
    }

  private:
    msgc::Ptr<IP2PNetworkCom> m_sp;
    HRESULT                   m_hr;
};

} // namespace p2p
