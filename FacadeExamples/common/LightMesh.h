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
// LightMesh.h
//
// The extra scaffolding the TWO mesh harnesses need -- WsaStoreTest and
// WsaQueryTest -- and nothing else in this tree includes it.
//
// Those two are the only harnesses here that touch both kernels, so they are
// the only ones that pull in the second facade (TargetFacade). This header is
// the seam: a timestamped log, a "did it happen yet" gate, and the endpoint
// composers, all of which the sibling tree ..\..\_Targetcore_UseExamples\FacadeExamples
// carries in its own LightHarness.h.

#pragma once

#include "LightHarness.h"
#include "TargetFacadeFn.hpp"

namespace light {

// Timestamped, flushed milestone log -- the same shape as the originals'
// LogAt(), which every networked harness in ..\DirectExamples defined
// for itself.
inline void Log ( const wchar_t *role, const wchar_t *fmt, ... )
{
    SYSTEMTIME st; ::GetLocalTime ( &st );
    wprintf ( L"[%02d:%02d:%02d.%03d tid=%lu %s] "
            , st.wHour, st.wMinute, st.wSecond, st.wMilliseconds
            , ::GetCurrentThreadId ( ), role );

    va_list ap;
    va_start ( ap, fmt );
    vwprintf ( fmt, ap );
    va_end ( ap );

    wprintf ( L"\n" );
}

// ---------------------------------------------------------------------------
// Gate -- a manual "did the thing happen yet" event. Replaces the raw
// CreateEvent / WaitForSingleObject / CloseHandle triplet each original
// repeated, and the `static HANDLE g_hDoneEvent` that went with it.
// ---------------------------------------------------------------------------
class Gate
{
    public:
        Gate ( ) { m_h = ::CreateEvent ( NULL, TRUE, FALSE, NULL ); }   // manual reset
       ~Gate ( ) { if ( m_h ) ::CloseHandle ( m_h ); }

        Gate             ( const Gate& ) = delete;
        Gate& operator = ( const Gate& ) = delete;

      void open ( )           { if ( m_h ) ::SetEvent ( m_h ); }
      bool wait ( DWORD ms )  { return m_h && ::WaitForSingleObject ( m_h, ms ) == WAIT_OBJECT_0; }

    private:
        HANDLE m_h;
};

// ---------------------------------------------------------------------------
// Endpoint composers. The facade has ONE arming pair -- listen(toPeer,
// endpoint) / connect(toPeer, endpoint) -- and the transport lives in the
// endpoint STRING rather than in the method name.
// ---------------------------------------------------------------------------
inline std::wstring TcpListen ( unsigned short port )
{
    // A listen must NOT name a host: the kernel binds INADDR_ANY regardless.
    return L"tcp://:" + std::to_wstring ( (unsigned)port );
}
inline std::wstring TcpDial ( const wchar_t *host, unsigned short port )
{
    return std::wstring ( L"tcp://" ) + host + L":" + std::to_wstring ( (unsigned)port );
}

// The messaging facade's own error names, for the arming calls.
inline const wchar_t* MeshHrName ( HRESULT hr )
{
    if ( hr == S_OK )                        return L"S_OK";
    if ( hr == S_FALSE )                     return L"S_FALSE";
    // A SUCCESS code, not an error: the link IS armed, but the two addresses
    // are neither ancestor nor descendant, so it can never be a transit hop.
    // Every Server/Client pair in this tree is a sibling pair, so this is the
    // expected answer from the arming calls, and FAILED() correctly does not
    // see it.
    if ( hr == p2pf::P2PF_S_UNRELATED_LINK ) return L"P2PF_S_UNRELATED_LINK";
    if ( hr == p2pf::P2PF_E_ABI_MISMATCH )   return L"P2PF_E_ABI_MISMATCH";
    if ( hr == p2pf::P2PF_E_STARTUP )        return L"P2PF_E_STARTUP";
    if ( hr == p2pf::P2PF_E_HUB_SPAWN )      return L"P2PF_E_HUB_SPAWN";
    if ( hr == p2pf::P2PF_E_CON_FACTORY )    return L"P2PF_E_CON_FACTORY";
    if ( hr == p2pf::P2PF_E_CON_DUPLICATE )  return L"P2PF_E_CON_DUPLICATE";
    if ( hr == p2pf::P2PF_E_RESERVED_TOPIC ) return L"P2PF_E_RESERVED_TOPIC";
    if ( hr == p2pf::P2PF_E_CLOSED )         return L"P2PF_E_CLOSED";
    if ( hr == p2pf::P2PF_E_HUB_DUPLICATE )  return L"P2PF_E_HUB_DUPLICATE";
    if ( hr == p2pf::P2PF_E_ENDPOINT )       return L"P2PF_E_ENDPOINT";
    if ( hr == p2pf::P2PF_E_UNRESOLVED )     return L"P2PF_E_UNRESOLVED";
    if ( hr == p2pf::P2PF_E_NO_HUB )         return L"P2PF_E_NO_HUB";
    if ( hr == p2pf::P2PF_E_LINK_PARTIAL )   return L"P2PF_E_LINK_PARTIAL";
    return L"(other)";
}

} // namespace light
