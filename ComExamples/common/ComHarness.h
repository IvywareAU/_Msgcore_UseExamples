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
// ComHarness.h
//
// The client-side layer that makes a raw COM client of MsgcoreCom as pleasant to
// write as a Msgcore client -- and, deliberately, uses NOTHING but plain COM to
// do it. No ATL, no MFC, no _com_ptr_t, no #import: CoCreateInstance, vtable
// calls, SysAllocString and a hand-written IDispatch sink. If the layer needed a
// framework to be usable, that would be worth knowing; it does not.
//
// This is the counterpart of _TargetCore_UseExamples\ComExamples\common\ComHarness.h, one tier
// down: that one wraps TargetCom (moving messages), this one wraps MsgcoreCom
// (what is IN a message). The parallel with the C++ original is close enough to
// read side by side:
//
//   P3PmsgField oRoot(L"Order");            msgc::Store  store;
//   oRoot.DeclareItem(L"Id", (int)10045);   store.root().declare(L"Id", 10045);
//   oRoot.SelectItem(L"Id").c_int()         store.root().child(L"Id").asLong()
//   oRoot.r_Attr(AttrCMD_Create) += ...     f.attributes().declare(L"Unit", L"C")
//   oMgr.Save(L"x.p2p")                     store.save(L"x.p2p")
//
// Underneath, every line of the right-hand column is doing more work: a BSTR
// per name, a VARIANT per value, an HRESULT per call, and -- for the store's
// change events -- an apartment transition on every callback.
//
// APARTMENT CHOICE. Every harness runs single-threaded-apartment on purpose.
// STA is what this layer's real audience uses (VB, Office, WSH, most .NET UI
// hosts) and it is the case MsgcoreCom's Global-Interface-Table machinery
// exists for: change events are raised on a dispatch thread inside the DLL and
// have to be marshalled into this thread. The cost is that an STA MUST PUMP
// MESSAGES or nothing is ever delivered -- so msgc::Gate::wait() pumps while it
// waits, and every callback below therefore runs on the main thread.
//
// Exit-code contract, identical to DirectExamples:
//   0 = SUCCESS   1 = SETUP failure (server not registered)
//   2 = ASSERT    3 = a check failed, or a networked harness timed out

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <olectl.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "MsgcoreCom_h.h"

// The GUID definitions (CLSID_MsgStore, IID_IMsgFieldCom, DIID__IMsgStoreEvents,
// ...). Every harness here is a single-translation-unit program, so defining
// them from the header keeps the harnesses free of COM boilerplate. Define
// COMHARNESS_NO_GUIDS in any additional .cpp that includes this header.
#ifndef COMHARNESS_NO_GUIDS
  #include "MsgcoreCom_i.c"
#endif

namespace msgc {

const int EXIT_SUCCESS_ = 0;
const int EXIT_SETUP    = 1;
const int EXIT_ASSERT   = 2;
const int EXIT_FAIL     = 3;

// ---------------------------------------------------------------------------
// The MsgcoreError values, spelled out for a raw vtable client. A type-library
// client gets them as named constants off the MsgcoreError enum; these are the
// same numbers.
//
// THESE ARE THE ERROR CHANNEL. The C++ Msgcore API throws a P2Pevent*; this one
// returns an HRESULT and leaves an IErrorInfo carrying a sentence. Every
// harness in this tree that would have written `catch (P2Pevent*)` writes
// `if (hr == msgc::E_...)` instead, and reads the sentence with LastError().
// ---------------------------------------------------------------------------
const HRESULT E_CLOSED    = MAKE_HRESULT(1, FACILITY_ITF, 0x0300);
const HRESULT E_LOAD      = MAKE_HRESULT(1, FACILITY_ITF, 0x0301);
const HRESULT E_SAVE      = MAKE_HRESULT(1, FACILITY_ITF, 0x0302);
const HRESULT E_NO_FIELD  = MAKE_HRESULT(1, FACILITY_ITF, 0x0303);
const HRESULT E_NO_POS    = MAKE_HRESULT(1, FACILITY_ITF, 0x0304);
const HRESULT E_TYPE      = MAKE_HRESULT(1, FACILITY_ITF, 0x0305);
const HRESULT E_DECLARE   = MAKE_HRESULT(1, FACILITY_ITF, 0x0306);
const HRESULT E_NOT_LIST  = MAKE_HRESULT(1, FACILITY_ITF, 0x0307);
const HRESULT E_NOT_VECT  = MAKE_HRESULT(1, FACILITY_ITF, 0x0308);
const HRESULT E_NO_COLL   = MAKE_HRESULT(1, FACILITY_ITF, 0x0309);
const HRESULT E_RANGE     = MAKE_HRESULT(1, FACILITY_ITF, 0x030A);
// 0x030B was msgcDetached: "you wrote through a copy and the write went
// nowhere". The server sits on MsgFacade now, where a node is a PATH re-resolved
// per call, so there is no detached node left to answer it. The constant is kept
// -- it is a documented number, and keeping it is cheaper than explaining a hole
// -- and E_DETACHED_RESERVED is spelled that way so nothing accidentally tests
// for a code that can no longer arrive.
const HRESULT E_DETACHED_RESERVED = MAKE_HRESULT(1, FACILITY_ITF, 0x030B);
const HRESULT E_KERNEL    = MAKE_HRESULT(1, FACILITY_ITF, 0x030C);
const HRESULT E_STALE     = MAKE_HRESULT(1, FACILITY_ITF, 0x030D);
const HRESULT E_NAME      = MAKE_HRESULT(1, FACILITY_ITF, 0x030E);
const HRESULT E_NO_SINK   = MAKE_HRESULT(1, FACILITY_ITF, 0x030F);
const HRESULT E_FOREIGN   = MAKE_HRESULT(1, FACILITY_ITF, 0x0310);
const HRESULT E_PAGESTATE = MAKE_HRESULT(1, FACILITY_ITF, 0x0311);
const HRESULT E_PATH      = MAKE_HRESULT(1, FACILITY_ITF, 0x0312);
const HRESULT E_LIMIT     = MAKE_HRESULT(1, FACILITY_ITF, 0x0313);

inline LPCWSTR HrName ( HRESULT hr )
{
    if ( hr == S_OK )       return L"S_OK";
    if ( hr == S_FALSE )    return L"S_FALSE";
    if ( hr == E_POINTER )  return L"E_POINTER";
    if ( hr == E_INVALIDARG ) return L"E_INVALIDARG";
    if ( hr == DISP_E_TYPEMISMATCH ) return L"DISP_E_TYPEMISMATCH";
    if ( hr == REGDB_E_CLASSNOTREG ) return L"REGDB_E_CLASSNOTREG";
    if ( hr == E_CLOSED )   return L"msgcClosed";
    if ( hr == E_LOAD )     return L"msgcLoad";
    if ( hr == E_SAVE )     return L"msgcSave";
    if ( hr == E_NO_FIELD ) return L"msgcNoField";
    if ( hr == E_NO_POS )   return L"msgcNoPos";
    if ( hr == E_TYPE )     return L"msgcType";
    if ( hr == E_DECLARE )  return L"msgcDeclare";
    if ( hr == E_NOT_LIST ) return L"msgcNotList";
    if ( hr == E_NOT_VECT ) return L"msgcNotVect";
    if ( hr == E_NO_COLL )  return L"msgcNoColl";
    if ( hr == E_RANGE )    return L"msgcRange";
    if ( hr == E_DETACHED_RESERVED ) return L"msgcDetached (reserved, unreachable)";
    if ( hr == E_KERNEL )   return L"msgcKernel";
    if ( hr == E_STALE )    return L"msgcStale";
    if ( hr == E_NAME )     return L"msgcName";
    if ( hr == E_NO_SINK )  return L"msgcNoSink";
    if ( hr == E_FOREIGN )  return L"msgcForeign";
    if ( hr == E_PAGESTATE ) return L"msgcPageState";
    if ( hr == E_PATH )     return L"msgcPath";
    if ( hr == E_LIMIT )    return L"msgcLimit";
    return L"(other)";
}

// The MsgDataType codes, for DeclareTyped and for reading DataType back.
const LONG TYPE_NULL   = 0;
const LONG TYPE_INT08  = 1;
const LONG TYPE_UINT08 = 2;
const LONG TYPE_INT16  = 3;
const LONG TYPE_UINT16 = 4;
const LONG TYPE_INT32  = 5;
const LONG TYPE_UINT32 = 6;
const LONG TYPE_INT64  = 7;
const LONG TYPE_UINT64 = 8;
const LONG TYPE_FLOAT  = 9;
const LONG TYPE_DOUBLE = 10;
const LONG TYPE_BOOL   = 13;
const LONG TYPE_BSTR   = 18;
const LONG TYPE_WSTR   = 26;
const LONG TYPE_BLOB   = 34;
const LONG TYPE_GUID   = 47;

// MsgAddrMode
const LONG ADDR_16 = 1;
const LONG ADDR_32 = 2;
const LONG ADDR_64 = 3;

// MsgTriggerFlag
const LONG TRIGGER_INSERT = 1;
const LONG TRIGGER_UPDATE = 2;
const LONG TRIGGER_DELETE = 4;
const LONG TRIGGER_ACTIVE = 8;
const LONG TRIGGER_ALL    = 15;

// ---------------------------------------------------------------------------
// The check harness. Same shape and same exit codes as the C++ tree's, so the
// two sets of logs are directly comparable line for line.
// ---------------------------------------------------------------------------
inline int g_nChecks = 0;
inline int g_nFailed = 0;

inline void Check ( bool bOk, LPCWSTR lpszWhat, int nLine )
{
    ++g_nChecks;
    if ( bOk ) return;
    ++g_nFailed;
    wprintf ( L"  FAIL (line %d): %s\n", nLine, lpszWhat );
    fflush ( stdout );
}
#define CHECK(expr) msgc::Check ( (expr), L#expr, __LINE__ )

inline void Section ( LPCWSTR lpszTitle )
{
    wprintf ( L"\n--- %s\n", lpszTitle );
    fflush ( stdout );
}

inline void Note ( LPCWSTR fmt, ... )
{
    wprintf ( L"  " );
    va_list ap; va_start ( ap, fmt );
    vwprintf ( fmt, ap );
    va_end ( ap );
    wprintf ( L"\n" );
    fflush ( stdout );
}

inline void Log ( LPCWSTR role, LPCWSTR fmt, ... )
{
    SYSTEMTIME st; ::GetLocalTime ( &st );
    wprintf ( L"[%02d:%02d:%02d.%03d tid=%lu %s] ",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
              ::GetCurrentThreadId(), role );
    va_list ap; va_start ( ap, fmt );
    vwprintf ( fmt, ap );
    va_end ( ap );
    wprintf ( L"\n" );
    fflush ( stdout );
}

inline int __cdecl AssertReportHook ( int nReportType, char *szMsg, int *pnRet )
{
    if ( nReportType == _CRT_ASSERT )
    {
        fflush ( stdout );
        fprintf ( stderr, "\n=== ASSERT TRIPPED ===\n%s\n", szMsg ? szMsg : "(no message)" );
        fflush ( stderr );
        if ( pnRet ) *pnRet = 0;        // don't invoke the debugger
        ExitProcess ( EXIT_ASSERT );
    }
    return FALSE;
}

inline void InitConsole ( )
{
    _setmode ( _fileno(stdout), _O_U16TEXT );
    _CrtSetReportMode ( _CRT_ASSERT, _CRTDBG_MODE_FILE );
    _CrtSetReportFile ( _CRT_ASSERT, _CRTDBG_FILE_STDERR );
    _CrtSetReportHook ( AssertReportHook );
}

inline int Verdict ( )
{
    const int nExit = ( g_nFailed == 0 ) ? EXIT_SUCCESS_ : EXIT_FAIL;
    wprintf ( L"\n%d checks, %d failed. Done (exit=%d).\n", g_nChecks, g_nFailed, nExit );
    fflush ( stdout );
    return nExit;
}

// ---------------------------------------------------------------------------
// Bstr / Var -- the smallest possible RAII for the two COM types these
// harnesses cannot avoid.
// ---------------------------------------------------------------------------
class Bstr
{
  public:
    explicit Bstr ( LPCWSTR s = NULL ) { m_bs = ::SysAllocString ( s ? s : L"" ); }
    Bstr ( const Bstr& o )             { m_bs = ::SysAllocString ( o.m_bs ? o.m_bs : L"" ); }
   ~Bstr ( )                           { ::SysFreeString ( m_bs ); }
    Bstr& operator = ( const Bstr& o )
    {
        if ( this != &o ) { ::SysFreeString ( m_bs ); m_bs = ::SysAllocString ( o.m_bs ? o.m_bs : L"" ); }
        return *this;
    }
    operator BSTR ( ) const { return m_bs; }
  private:
    BSTR m_bs;
};

// Adopts a BSTR an [out] parameter handed us, and frees it.
inline std::wstring Take ( BSTR bs )
{
    std::wstring s ( bs ? bs : L"" );
    ::SysFreeString ( bs );
    return s;
}

class Var
{
  public:
    Var ( )                  { ::VariantInit ( &m_v ); }
    Var ( LONG v )           { ::VariantInit ( &m_v ); m_v.vt = VT_I4;   m_v.lVal    = v; }
    Var ( int v )            { ::VariantInit ( &m_v ); m_v.vt = VT_I4;   m_v.lVal    = v; }
    Var ( LONGLONG v )       { ::VariantInit ( &m_v ); m_v.vt = VT_I8;   m_v.llVal   = v; }
    Var ( double v )         { ::VariantInit ( &m_v ); m_v.vt = VT_R8;   m_v.dblVal  = v; }
    Var ( bool v )           { ::VariantInit ( &m_v ); m_v.vt = VT_BOOL; m_v.boolVal = v ? VARIANT_TRUE : VARIANT_FALSE; }
    Var ( LPCWSTR v )        { ::VariantInit ( &m_v ); m_v.vt = VT_BSTR; m_v.bstrVal = ::SysAllocString ( v ? v : L"" ); }
    Var ( const Var& o )     { ::VariantInit ( &m_v ); ::VariantCopy ( &m_v, const_cast<VARIANT*>(&o.m_v) ); }
   ~Var ( )                  { ::VariantClear ( &m_v ); }

    Var& operator = ( const Var& o )
    {
        if ( this != &o ) { ::VariantClear ( &m_v ); ::VariantCopy ( &m_v, const_cast<VARIANT*>(&o.m_v) ); }
        return *this;
    }

    // A byte array, which is what a BLOB16 node reads and writes as.
    static Var bytes ( const void *p, unsigned int cb )
    {
        Var v;
        SAFEARRAY *psa = ::SafeArrayCreateVector ( VT_UI1, 0, cb );
        if ( psa != NULL )
        {
            if ( cb != 0 )
            {
                void *dst = NULL;
                if ( SUCCEEDED ( ::SafeArrayAccessData ( psa, &dst ) ) )
                {
                    ::memcpy ( dst, p, cb );
                    ::SafeArrayUnaccessData ( psa );
                }
            }
            v.m_v.vt = VT_ARRAY | VT_UI1;
            v.m_v.parray = psa;
        }
        return v;
    }

    VARIANT& raw ( )             { return m_v; }
    const VARIANT& raw ( ) const { return m_v; }
    VARIANT* addr ( )            { ::VariantClear ( &m_v ); return &m_v; }
    VARTYPE  vt ( ) const        { return m_v.vt; }

    bool isEmpty ( ) const { return m_v.vt == VT_EMPTY; }

    LONG asLong ( ) const
    {
        VARIANT t; ::VariantInit ( &t );
        LONG r = 0;
        if ( SUCCEEDED ( ::VariantChangeType ( &t, const_cast<VARIANT*>(&m_v), 0, VT_I4 ) ) ) r = t.lVal;
        ::VariantClear ( &t );
        return r;
    }
    LONGLONG asInt64 ( ) const
    {
        VARIANT t; ::VariantInit ( &t );
        LONGLONG r = 0;
        if ( SUCCEEDED ( ::VariantChangeType ( &t, const_cast<VARIANT*>(&m_v), 0, VT_I8 ) ) ) r = t.llVal;
        ::VariantClear ( &t );
        return r;
    }
    double asDouble ( ) const
    {
        VARIANT t; ::VariantInit ( &t );
        double r = 0;
        if ( SUCCEEDED ( ::VariantChangeType ( &t, const_cast<VARIANT*>(&m_v), 0, VT_R8 ) ) ) r = t.dblVal;
        ::VariantClear ( &t );
        return r;
    }
    bool asBool ( ) const
    {
        VARIANT t; ::VariantInit ( &t );
        bool r = false;
        if ( SUCCEEDED ( ::VariantChangeType ( &t, const_cast<VARIANT*>(&m_v), 0, VT_BOOL ) ) )
            r = ( t.boolVal != VARIANT_FALSE );
        ::VariantClear ( &t );
        return r;
    }
    std::wstring asText ( ) const
    {
        VARIANT t; ::VariantInit ( &t );
        std::wstring r;
        if ( SUCCEEDED ( ::VariantChangeType ( &t, const_cast<VARIANT*>(&m_v), 0, VT_BSTR ) ) && t.bstrVal )
            r = t.bstrVal;
        ::VariantClear ( &t );
        return r;
    }
    std::vector<BYTE> asBytes ( ) const
    {
        std::vector<BYTE> out;
        if ( ( m_v.vt & VT_ARRAY ) == 0 || m_v.parray == NULL ) return out;
        LONG lo = 0, hi = -1;
        ::SafeArrayGetLBound ( m_v.parray, 1, &lo );
        ::SafeArrayGetUBound ( m_v.parray, 1, &hi );
        if ( hi < lo ) return out;
        void *p = NULL;
        if ( SUCCEEDED ( ::SafeArrayAccessData ( m_v.parray, &p ) ) )
        {
            const BYTE *b = (const BYTE*)p;
            out.assign ( b, b + ( hi - lo + 1 ) );
            ::SafeArrayUnaccessData ( m_v.parray );
        }
        return out;
    }

  private:
    VARIANT m_v;
};

// ---------------------------------------------------------------------------
// The sentence behind the last failure. This is what replaces P2Pevent's
// GetMessage(): MsgcoreCom implements ISupportErrorInfo and leaves an
// IErrorInfo on the calling thread, so a failed call can be EXPLAINED and not
// merely numbered.
// ---------------------------------------------------------------------------
inline std::wstring LastError ( )
{
    IErrorInfo *pei = NULL;
    if ( ::GetErrorInfo ( 0, &pei ) != S_OK || pei == NULL ) return std::wstring();

    BSTR bs = NULL;
    pei->GetDescription ( &bs );
    pei->Release();
    return Take ( bs );
}

// Print it, truncated to one readable line's worth.
inline void ShowError ( LPCWSTR what, HRESULT hr )
{
    std::wstring s = LastError();
    if ( s.size() > 160 ) { s.resize ( 157 ); s += L"..."; }
    wprintf ( L"  %s -> 0x%08lX %s\n", what, (unsigned long)hr, HrName ( hr ) );
    if ( !s.empty() ) wprintf ( L"     %s\n", s.c_str() );
    fflush ( stdout );
}

// ---------------------------------------------------------------------------
// Apartment -- CoInitializeEx/CoUninitialize, and the message pump an STA owes
// the runtime.
// ---------------------------------------------------------------------------
class Apartment
{
  public:
    Apartment ( )     { m_hr = ::CoInitializeEx ( NULL, COINIT_APARTMENTTHREADED ); }
   ~Apartment ( )     { if ( SUCCEEDED(m_hr) ) ::CoUninitialize(); }
    bool ok ( ) const { return SUCCEEDED(m_hr); }
  private:
    HRESULT m_hr;
};

inline void Pump ( )
{
    MSG msg;
    while ( ::PeekMessage ( &msg, NULL, 0, 0, PM_REMOVE ) )
    {
        ::TranslateMessage ( &msg );
        ::DispatchMessage ( &msg );
    }
}

// A "did it happen yet" counter whose wait() PUMPS -- without that, a
// marshalled event can never be delivered to this apartment and every harness
// that waits for one would hang.
class Gate
{
  public:
    Gate ( ) : m_count ( 0 ) { }
    void bump   ( )       { ::InterlockedIncrement ( &m_count ); }
    LONG count  ( ) const { return ::InterlockedCompareExchange ( (volatile LONG*)&m_count, 0, 0 ); }
    void reset  ( )       { ::InterlockedExchange ( &m_count, 0 ); }
    bool wait   ( LONG want, DWORD ms )
    {
        DWORD start = ::GetTickCount();
        for ( ;; )
        {
            Pump();
            if ( count() >= want ) return true;
            if ( ::GetTickCount() - start > ms ) return false;
            ::Sleep ( 5 );
        }
    }
  private:
    volatile LONG m_count;
};

inline void PumpFor ( DWORD ms )
{
    DWORD start = ::GetTickCount();
    while ( ::GetTickCount() - start < ms ) { Pump(); ::Sleep ( 5 ); }
}

// ---------------------------------------------------------------------------
// Ptr<T> -- the minimum COM smart pointer. Not CComPtr: this tree links no ATL.
// ---------------------------------------------------------------------------
template <class T>
class Ptr
{
  public:
    Ptr ( )            : m_p ( NULL ) { }
    Ptr ( const Ptr& o ) : m_p ( o.m_p ) { if ( m_p ) m_p->AddRef(); }
   ~Ptr ( )            { release(); }

    Ptr& operator = ( const Ptr& o )
    {
        if ( this != &o ) { if ( o.m_p ) o.m_p->AddRef(); release(); m_p = o.m_p; }
        return *this;
    }

    void release ( ) { if ( m_p ) { m_p->Release(); m_p = NULL; } }
    void attach  ( T *p ) { release(); m_p = p; }        // takes the reference

    T** addr ( )  { release(); return &m_p; }
    T*  get  ( ) const { return m_p; }
    T*  operator -> ( ) const { return m_p; }
    bool ok ( ) const { return m_p != NULL; }

  private:
    T *m_p;
};

// ---------------------------------------------------------------------------
// EVERY ACCESSOR BELOW IS NULL-SAFE, and that is not defensive padding -- it is
// the difference between a harness that reports a failure and one that dies of
// it.
//
// These wrappers are handed out by calls that can FAIL: Store::root() when the
// store is closed, Field::child() when there is no such name, Attr::item() on
// a missing attribute. Each of those leaves the wrapper empty, and the very
// next line a caller writes is almost always a read:
//
//      CHECK ( store.root().count() == 0 );        // root() may have failed
//
// Dereferencing there is an access violation INSIDE THE CHECK, which loses the
// check, the line number, the exit code and every section after it -- exactly
// the information the harness exists to produce. So an empty wrapper answers a
// benign default and ok() is how a caller asks whether it got one.
// ---------------------------------------------------------------------------
#define MSGC_IF_NULL(ret) if ( !m_p.ok() ) return ret

class Field;
class Attr;
class Desc;
class Cursor;
class Walker;
class List;
class Vect;

// ---------------------------------------------------------------------------
// Field -- IMsgFieldCom with Msgcore-shaped verbs.
//
// This class used to carry the LIVE / DETACHED split in one place: child() was
// live, item() was a read-only deep copy, and isLive() was the question that
// stopped a write from silently vanishing. ALL THREE ARE GONE. The server sits
// on MsgFacade now, where a node is the route to itself and every one of them is
// live, so there is no copy to hand out and no question to ask. child() is the
// one lookup.
// ---------------------------------------------------------------------------
class Field
{
  public:
    Field ( ) { }

    IMsgFieldCom** addr ( ) { return m_p.addr(); }
    IMsgFieldCom*  raw  ( ) const { return m_p.get(); }
    bool ok ( ) const { return m_p.ok(); }

    // --- identity -----------------------------------------------------------
    std::wstring name ( ) const  { MSGC_IF_NULL(std::wstring()); BSTR bs = NULL; m_p->get_Name ( &bs ); return Take ( bs ); }
    std::wstring path ( ) const  { MSGC_IF_NULL(std::wstring()); BSTR bs = NULL; m_p->get_Path ( &bs ); return Take ( bs ); }
    LONGLONG     p2pos ( ) const { MSGC_IF_NULL(0); LONGLONG v = 0; m_p->get_P2Pos ( &v ); return v; }

    // --- value --------------------------------------------------------------
    Var value ( ) const { MSGC_IF_NULL(Var()); Var v; m_p->get_Value ( v.addr() ); return v; }
    HRESULT setValue ( const Var& v ) { MSGC_IF_NULL(E_POINTER); return m_p->put_Value ( const_cast<VARIANT&>(v.raw()) ); }

    LONG         asLong   ( ) const { return value().asLong(); }
    LONGLONG     asInt64  ( ) const { return value().asInt64(); }
    double       asDouble ( ) const { return value().asDouble(); }
    bool         asBool   ( ) const { return value().asBool(); }
    std::wstring asText   ( ) const { return value().asText(); }
    std::vector<BYTE> asBytes ( ) const { return value().asBytes(); }

    std::wstring text ( ) const { MSGC_IF_NULL(std::wstring()); BSTR bs = NULL; m_p->get_Text ( &bs ); return Take ( bs ); }
    HRESULT setText ( LPCWSTR s ) { MSGC_IF_NULL(E_POINTER); return m_p->put_Text ( Bstr(s) ); }

    LONG         dataType ( ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_DataType ( &v ); return v; }
    std::wstring typeName ( ) const { MSGC_IF_NULL(std::wstring()); BSTR bs = NULL; m_p->get_TypeName ( &bs ); return Take ( bs ); }
    bool isNull ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsNull ( &b ); return b != VARIANT_FALSE; }
    // isVoid() is gone with IsVoid. It asked whether a node had a data cell yet
    // as distinct from holding a NULL value -- a distinction the store does not
    // actually draw, so the two answers were never reliably different. isNull()
    // is the question that survives.

    // --- children -----------------------------------------------------------
    bool exists ( LPCWSTR n ) const
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->Exists ( Bstr(n), &b );
        return b != VARIANT_FALSE;
    }

    // Child now reaches a LIST and a VECT too, which it could not before: it
    // used to resolve through SelectItem, which throws on a container.
    HRESULT child ( LPCWSTR n, Field& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->Child ( Bstr(n), out.addr() ); }

    // Convenience for the common "I know it is there" case; leaves `out` empty
    // when it is not, which ok() reports.
    Field child ( LPCWSTR n ) const { MSGC_IF_NULL(Field()); Field f; m_p->Child ( Bstr(n), f.addr() ); return f; }

    // By POSITION rather than by name, for walking without a cursor.
    HRESULT childAt ( LONG i, Field& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->ChildAt ( i, out.addr() ); }
    Field   childAt ( LONG i ) const { MSGC_IF_NULL(Field()); Field f; m_p->ChildAt ( i, f.addr() ); return f; }

    HRESULT declare ( LPCWSTR n, const Var& v, Field *pOut = NULL, bool bUpdate = true )
    {
        MSGC_IF_NULL(E_POINTER);
        Field tmp;
        Field& out = pOut ? *pOut : tmp;
        return m_p->Declare ( Bstr(n), const_cast<VARIANT&>(v.raw()),
                              bUpdate ? VARIANT_TRUE : VARIANT_FALSE, out.addr() );
    }

    HRESULT declareTyped ( LPCWSTR n, const Var& v, LONG dataType, Field *pOut = NULL, bool bUpdate = true )
    {
        MSGC_IF_NULL(E_POINTER);
        Field tmp;
        Field& out = pOut ? *pOut : tmp;
        return m_p->DeclareTyped ( Bstr(n), const_cast<VARIANT&>(v.raw()), dataType,
                                   bUpdate ? VARIANT_TRUE : VARIANT_FALSE, out.addr() );
    }

    bool remove ( LPCWSTR n )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->Delete ( Bstr(n), &b );
        return b != VARIANT_FALSE;
    }

    HRESULT truncate ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Truncate(); }

    bool renameChild ( LPCWSTR oldName, LPCWSTR newName )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->RenameChild ( Bstr(oldName), Bstr(newName), &b );
        return b != VARIANT_FALSE;
    }

    bool moveChild ( const Field& dest, LPCWSTR n )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->MoveChild ( dest.raw(), Bstr(n), &b );
        return b != VARIANT_FALSE;
    }

    // The same call with the HRESULT kept, for the cases that are ABOUT the
    // refusal (msgcForeign) rather than about the move.
    HRESULT moveChildHr ( const Field& dest, LPCWSTR n )
    {
        MSGC_IF_NULL(E_POINTER);
        VARIANT_BOOL b = VARIANT_FALSE;
        return m_p->MoveChild ( dest.raw(), Bstr(n), &b );
    }

    // Takes a TYPE, not a value. It used to take a VARIANT, because the flat
    // ABI's retype entry points were one per type and passing a value was the
    // only way to pick one -- but a retype seeds a ZERO by definition, so that
    // argument was a fiction: passing 7 to make an INT32 read as though it
    // stored 7.
    bool retypeChild ( LPCWSTR n, LONG dataType )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->RetypeChild ( Bstr(n), dataType, &b );
        return b != VARIANT_FALSE;
    }
    HRESULT retypeChildHr ( LPCWSTR n, LONG dataType )
    {
        MSGC_IF_NULL(E_POINTER);
        VARIANT_BOOL b = VARIANT_FALSE;
        return m_p->RetypeChild ( Bstr(n), dataType, &b );
    }

    LONG count ( ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_Count ( &v ); return v; }

    // --- shape --------------------------------------------------------------
    bool isList       ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsList ( &b );       return b != VARIANT_FALSE; }
    bool isVect       ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsVect ( &b );       return b != VARIANT_FALSE; }
    // isAttr() and isDesc() are gone with IsAttr / IsDesc. They asked "is this
    // node ITSELF a collection object", and nothing answers to that any more: a
    // collection is a SCOPE of a node, not a thing a node can be.
    bool isStacked    ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsStacked ( &b );    return b != VARIANT_FALSE; }
    bool isAttributed ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsAttributed ( &b ); return b != VARIANT_FALSE; }
    bool isDescendant ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsDescendant ( &b ); return b != VARIANT_FALSE; }

    // --- timestamp ----------------------------------------------------------
    DATE    timestamp ( ) const   { MSGC_IF_NULL((DATE)0); DATE d = 0; m_p->get_Timestamp ( &d ); return d; }
    HRESULT setTimestamp ( DATE d ) { MSGC_IF_NULL(E_POINTER); return m_p->put_Timestamp ( d ); }
    HRESULT touch ( )             { MSGC_IF_NULL(E_POINTER); return m_p->Touch(); }

    // --- views (defined after the view classes) ------------------------------
    HRESULT attributes  ( Attr& out, bool bCreate = true ) const;
    HRESULT descendants ( Desc& out, bool bCreate = true ) const;
    HRESULT cursor      ( Cursor& out ) const;
    HRESULT list        ( List& out ) const;
    HRESULT vector      ( Vect& out ) const;

    // --- child containers, and the two live walkers -------------------------
    // childList / childVect used to exist because child() COULD NOT reach a
    // container: it resolved through SelectItem, which throws on a list or a
    // vect. child() can now, so these are the TYPED spellings -- they answer
    // msgcNotList / msgcNotVect for a name that exists but is the other kind,
    // so neither needs a pre-check.
    HRESULT declareList ( LPCWSTR name, List& out ) const;
    HRESULT declareVect ( LPCWSTR name, LONG count, LONG dataType, Vect& out ) const;
    HRESULT childList   ( LPCWSTR name, List& out ) const;
    HRESULT childVect   ( LPCWSTR name, Vect& out ) const;
    HRESULT walker      ( Walker& out ) const;

    // --- the value stack ----------------------------------------------------
    // These four replace the Stack class, which wrapped a whole IMsgStackCom
    // object. What that object was is ONE saved (name, value) pair living
    // INSIDE this node, so it is four members rather than a second object with
    // a lifetime of its own to get wrong.
    //
    // Not a general stack: one saved pair per node, and pushing twice replaces
    // the first. pop()/drop() answer whether they DID anything -- which the old
    // object's Pop did not, so a drain loop had to test IsEmpty separately.
    HRESULT pushValue ( ) { MSGC_IF_NULL(E_POINTER); return m_p->PushValue(); }
    bool popValue ( )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->PopValue ( &b );
        return b != VARIANT_FALSE;
    }
    bool dropValue ( )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->DropValue ( &b );
        return b != VARIANT_FALSE;
    }
    HRESULT popValueHr  ( ) { MSGC_IF_NULL(E_POINTER); VARIANT_BOOL b = VARIANT_FALSE; return m_p->PopValue ( &b ); }
    HRESULT dropValueHr ( ) { MSGC_IF_NULL(E_POINTER); VARIANT_BOOL b = VARIANT_FALSE; return m_p->DropValue ( &b ); }

    // For Each over the children, as a snapshot.
    HRESULT forEach ( std::function<void(Field&)> fn ) const;

  private:
    Ptr<IMsgFieldCom> m_p;
};

// ---------------------------------------------------------------------------
// The collection views.
// ---------------------------------------------------------------------------
class Attr
{
  public:
    IMsgAttrCom** addr ( ) { return m_p.addr(); }
    IMsgAttrCom*  raw  ( ) const { return m_p.get(); }
    bool ok ( ) const { return m_p.ok(); }

    LONG count   ( ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_Count ( &v ); return v; }
    bool isEmpty ( ) const { MSGC_IF_NULL(true); VARIANT_BOOL b = VARIANT_TRUE; m_p->get_IsEmpty ( &b ); return b != VARIANT_FALSE; }
    bool exists  ( LPCWSTR n ) const
    {
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->Exists ( Bstr(n), &b );
        return b != VARIANT_FALSE;
    }
    Field item ( LPCWSTR n ) const { MSGC_IF_NULL(Field()); Field f; m_p->get_Item ( Bstr(n), f.addr() ); return f; }

    HRESULT declare ( LPCWSTR n, const Var& v, bool bUpdate = true )
    {
        MSGC_IF_NULL(E_POINTER);
        Field out;
        return m_p->Declare ( Bstr(n), const_cast<VARIANT&>(v.raw()),
                              bUpdate ? VARIANT_TRUE : VARIANT_FALSE, out.addr() );
    }
    bool remove ( LPCWSTR n )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->Delete ( Bstr(n), &b );
        return b != VARIANT_FALSE;
    }
    HRESULT truncate ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Truncate(); }
    HRESULT cursor ( Cursor& out ) const;

  private:
    Ptr<IMsgAttrCom> m_p;
};

class Desc
{
  public:
    IMsgDescCom** addr ( ) { return m_p.addr(); }
    IMsgDescCom*  raw  ( ) const { return m_p.get(); }
    bool ok ( ) const { return m_p.ok(); }

    LONG count   ( ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_Count ( &v ); return v; }
    bool isEmpty ( ) const { MSGC_IF_NULL(true); VARIANT_BOOL b = VARIANT_TRUE; m_p->get_IsEmpty ( &b ); return b != VARIANT_FALSE; }
    bool exists  ( LPCWSTR n ) const
    {
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->Exists ( Bstr(n), &b );
        return b != VARIANT_FALSE;
    }
    Field item ( LPCWSTR n ) const { MSGC_IF_NULL(Field()); Field f; m_p->get_Item ( Bstr(n), f.addr() ); return f; }

    HRESULT declare ( LPCWSTR n, const Var& v, Field *pOut = NULL, bool bUpdate = true )
    {
        MSGC_IF_NULL(E_POINTER);
        Field tmp;
        Field& out = pOut ? *pOut : tmp;
        return m_p->Declare ( Bstr(n), const_cast<VARIANT&>(v.raw()),
                              bUpdate ? VARIANT_TRUE : VARIANT_FALSE, out.addr() );
    }
    bool remove ( LPCWSTR n )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->Delete ( Bstr(n), &b );
        return b != VARIANT_FALSE;
    }
    HRESULT truncate ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Truncate(); }
    HRESULT cursor ( Cursor& out ) const;

  private:
    Ptr<IMsgDescCom> m_p;
};

class Cursor
{
  public:
    IMsgCursorCom** addr ( ) { return m_p.addr(); }
    IMsgCursorCom*  raw  ( ) const { return m_p.get(); }
    bool ok ( ) const { return m_p.ok(); }

    LONG count ( ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_Count ( &v ); return v; }
    LONG index ( ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_Index ( &v ); return v; }
    std::wstring name ( ) const { MSGC_IF_NULL(std::wstring()); BSTR bs = NULL; m_p->get_Name ( &bs ); return Take ( bs ); }

    // The one that is NOT the flat library's predicate -- see IMsgCursorCom in
    // the type library. Here it means "walked off the end", so the natural loop
    //     for ( c.seek(); !c.eoc(); c.next() )
    // visits every element, which the flat IsEoCursor would not.
    bool eoc ( ) const { MSGC_IF_NULL(true); VARIANT_BOOL b = VARIANT_TRUE;  m_p->get_EndOfCursor ( &b );   return b != VARIANT_FALSE; }
    bool soc ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_StartOfCursor ( &b ); return b != VARIANT_FALSE; }

    HRESULT seek ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Seek(); }
    HRESULT next ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Next(); }

    bool gotoName ( LPCWSTR n )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->GotoName ( Bstr(n), &b );
        return b != VARIANT_FALSE;
    }
    bool gotoIndex ( LONG i )
    {
        MSGC_IF_NULL(false);
        VARIANT_BOOL b = VARIANT_FALSE;
        m_p->GotoIndex ( i, &b );
        return b != VARIANT_FALSE;
    }

    Field field ( ) const { MSGC_IF_NULL(Field()); Field f; m_p->get_Field ( f.addr() ); return f; }

    bool isItem ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsItem ( &b ); return b != VARIANT_FALSE; }
    bool isList ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsList ( &b ); return b != VARIANT_FALSE; }
    bool isVect ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsVect ( &b ); return b != VARIANT_FALSE; }

    HRESULT remove ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Delete(); }

  private:
    Ptr<IMsgCursorCom> m_p;
};

class List
{
  public:
    IMsgListCom** addr ( ) { return m_p.addr(); }
    bool ok ( ) const { return m_p.ok(); }

    LONG count ( ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_Count ( &v ); return v; }
    HRESULT addHead  ( const Var& v ) { MSGC_IF_NULL(E_POINTER); return m_p->AddHead ( const_cast<VARIANT&>(v.raw()) ); }
    HRESULT addTail  ( const Var& v ) { MSGC_IF_NULL(E_POINTER); return m_p->AddTail ( const_cast<VARIANT&>(v.raw()) ); }
    HRESULT dropHead ( ) { MSGC_IF_NULL(E_POINTER); return m_p->DropHead(); }
    HRESULT dropTail ( ) { MSGC_IF_NULL(E_POINTER); return m_p->DropTail(); }
    HRESULT truncate ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Truncate(); }

    Var item ( LONG i ) const { MSGC_IF_NULL(Var()); Var v; m_p->get_Item ( i, v.addr() ); return v; }
    HRESULT itemHr ( LONG i, Var& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->get_Item ( i, out.addr() ); }
    HRESULT setItem ( LONG i, const Var& v ) { MSGC_IF_NULL(E_POINTER); return m_p->put_Item ( i, const_cast<VARIANT&>(v.raw()) ); }
    LONG typeAt ( LONG i ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_TypeAt ( i, &v ); return v; }
    HRESULT typeAtHr ( LONG i, LONG *pOut ) const { MSGC_IF_NULL(E_POINTER); return m_p->get_TypeAt ( i, pOut ); }
    bool removeAt ( LONG i ) { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->RemoveAt ( i, &b ); return b != VARIANT_FALSE; }
    HRESULT newEnum ( IUnknown **ppUnk ) const { MSGC_IF_NULL(E_POINTER); return m_p->get__NewEnum ( ppUnk ); }

  private:
    Ptr<IMsgListCom> m_p;
};

class Vect
{
  public:
    IMsgVectCom** addr ( ) { return m_p.addr(); }
    bool ok ( ) const { return m_p.ok(); }

    Var item ( LONG i ) const { MSGC_IF_NULL(Var()); Var v; m_p->get_Item ( i, v.addr() ); return v; }
    bool isData  ( LONG i ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->IsData ( i, &b );  return b != VARIANT_FALSE; }
    bool isField ( LONG i ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->IsField ( i, &b ); return b != VARIANT_FALSE; }
    bool isList  ( LONG i ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->IsList ( i, &b );  return b != VARIANT_FALSE; }
    bool isVect  ( LONG i ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->IsVect ( i, &b );  return b != VARIANT_FALSE; }
    // field(i) is gone: there is no accessor below this tier for "the element at
    // an index AS A NODE". A vect's elements are addressed by index and reached
    // as VALUES (item), as a nested list (listAt) or as a nested vect (vectAt) --
    // not as nodes with paths of their own. nameAt() answers the one thing
    // field() was usually asked for.
    HRESULT truncate ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Truncate(); }

    LONG count ( ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_Count ( &v ); return v; }
    HRESULT setItem ( LONG i, const Var& v ) { MSGC_IF_NULL(E_POINTER); return m_p->put_Item ( i, const_cast<VARIANT&>(v.raw()) ); }
    LONG typeAt ( LONG i ) const { MSGC_IF_NULL(0); LONG v = 0; m_p->get_TypeAt ( i, &v ); return v; }
    HRESULT typeAtHr ( LONG i, LONG *pOut ) const { MSGC_IF_NULL(E_POINTER); return m_p->get_TypeAt ( i, pOut ); }
    std::wstring nameAt ( LONG i ) const { MSGC_IF_NULL(std::wstring()); BSTR bs = NULL; m_p->get_NameAt ( i, &bs ); return Take ( bs ); }
    HRESULT listAt ( LONG i, List& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->ListAt ( i, out.addr() ); }
    HRESULT vectAt ( LONG i, Vect& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->VectAt ( i, out.addr() ); }
    // insertAt is gone with it: it deep-copied a node into a slot and grew the
    // vector, and there is no insert below this tier to call. A vect is declared
    // with its length and its element prototype; removeAt compacts, and nothing
    // shifts the other way.
    bool removeAt ( LONG i ) { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->RemoveAt ( i, &b ); return b != VARIANT_FALSE; }
    HRESULT newEnum ( IUnknown **ppUnk ) const { MSGC_IF_NULL(E_POINTER); return m_p->get__NewEnum ( ppUnk ); }

  private:
    Ptr<IMsgVectCom> m_p;
};

// The Stack class is gone with IMsgStackCom. It wrapped the core's MsgStck as
// an object of its own -- Push / Pop / Drop / Rename / IsEmpty / Name / Field --
// and had two behaviours a caller had to normalise by hand: an unconnected stack
// reported itself as NOT empty, and Pop's success did not mean anything had been
// restored. What it wrapped is one saved (name, value) pair inside a node, so it
// is now Field::pushValue / popValue / dropValue / isStacked, with pop answering
// whether it restored anything.

// ---------------------------------------------------------------------------
// Walker -- IMsgRecursCom. A whole subtree from one flat loop, descending only
// where the caller asks, which is what lets it PRUNE.
// ---------------------------------------------------------------------------
class Walker
{
  public:
    IMsgRecursCom** addr ( ) { return m_p.addr(); }
    bool ok ( ) const { return m_p.ok(); }

    HRESULT moveNext ( ) { MSGC_IF_NULL(E_POINTER); return m_p->MoveNext(); }
    LONG push ( ) { MSGC_IF_NULL(-1); LONG d = -1; m_p->Push ( &d ); return d; }
    HRESULT pushHr ( LONG *pDepth ) { MSGC_IF_NULL(E_POINTER); return m_p->Push ( pDepth ); }
    LONG pop  ( ) { MSGC_IF_NULL(-1); LONG d = -1; m_p->Pop ( &d ); return d; }
    HRESULT breakOut ( ) { MSGC_IF_NULL(E_POINTER); return m_p->Break(); }
    bool atEnd ( ) const { MSGC_IF_NULL(true); VARIANT_BOOL b = VARIANT_TRUE; m_p->get_AtEnd ( &b ); return b != VARIANT_FALSE; }
    std::wstring name ( ) const { MSGC_IF_NULL(std::wstring()); BSTR bs = NULL; m_p->get_Name ( &bs ); return Take ( bs ); }
    bool isField ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsField ( &b ); return b != VARIANT_FALSE; }
    bool isList  ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsList ( &b ); return b != VARIANT_FALSE; }
    bool isVect  ( ) const { MSGC_IF_NULL(false); VARIANT_BOOL b = VARIANT_FALSE; m_p->get_IsVect ( &b ); return b != VARIANT_FALSE; }
    HRESULT field  ( Field& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->get_Field ( out.addr() ); }
    HRESULT list   ( List& out )  const { MSGC_IF_NULL(E_POINTER); return m_p->get_List ( out.addr() ); }
    HRESULT vector ( Vect& out )  const { MSGC_IF_NULL(E_POINTER); return m_p->get_Vector ( out.addr() ); }

    // Two new ones. The walker reports where it is and the server assembles a
    // path from that as the walk moves, so unlike everything else on this object
    // path() stays true after the walk has moved on.
    LONG depth ( ) const { MSGC_IF_NULL(-1); LONG d = -1; m_p->get_Depth ( &d ); return d; }
    std::wstring path ( ) const { MSGC_IF_NULL(std::wstring()); BSTR bs = NULL; m_p->get_Path ( &bs ); return Take ( bs ); }

  private:
    Ptr<IMsgRecursCom> m_p;
};

inline HRESULT Field::attributes ( Attr& out, bool bCreate ) const
{
    MSGC_IF_NULL(E_POINTER);
    return m_p->Attributes ( bCreate ? VARIANT_TRUE : VARIANT_FALSE, out.addr() );
}
inline HRESULT Field::descendants ( Desc& out, bool bCreate ) const
{
    MSGC_IF_NULL(E_POINTER);
    return m_p->Descendants ( bCreate ? VARIANT_TRUE : VARIANT_FALSE, out.addr() );
}
inline HRESULT Field::cursor ( Cursor& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->get_Cursor ( out.addr() ); }
inline HRESULT Field::list   ( List& out )   const { MSGC_IF_NULL(E_POINTER); return m_p->get_List ( out.addr() ); }
inline HRESULT Field::vector ( Vect& out )   const { MSGC_IF_NULL(E_POINTER); return m_p->get_Vector ( out.addr() ); }

inline HRESULT Field::declareList ( LPCWSTR name, List& out ) const
{ MSGC_IF_NULL(E_POINTER); Bstr b ( name ); return m_p->DeclareList ( b, out.addr() ); }
inline HRESULT Field::declareVect ( LPCWSTR name, LONG count, LONG dataType, Vect& out ) const
{ MSGC_IF_NULL(E_POINTER); Bstr b ( name ); return m_p->DeclareVect ( b, count, dataType, out.addr() ); }
inline HRESULT Field::childList ( LPCWSTR name, List& out ) const
{ MSGC_IF_NULL(E_POINTER); Bstr b ( name ); return m_p->ChildList ( b, out.addr() ); }
inline HRESULT Field::childVect ( LPCWSTR name, Vect& out ) const
{ MSGC_IF_NULL(E_POINTER); Bstr b ( name ); return m_p->ChildVect ( b, out.addr() ); }
inline HRESULT Field::walker ( Walker& out ) const
{ MSGC_IF_NULL(E_POINTER); return m_p->get_Walker ( out.addr() ); }
inline HRESULT Attr::cursor  ( Cursor& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->get_Cursor ( out.addr() ); }
inline HRESULT Desc::cursor  ( Cursor& out ) const { MSGC_IF_NULL(E_POINTER); return m_p->get_Cursor ( out.addr() ); }

// _NewEnum, i.e. what `For Each child In field` compiles to in a scripting
// host. A SNAPSHOT: the handler may delete as it walks.
// The same for a collection of VALUES rather than of nodes. A list entry and a
// vect element are data cells, not fields, so `For Each v In list` yields the
// VARIANT itself and there is no IDispatch to query -- which is why EnumEach
// below would silently visit nothing over either of them.
inline HRESULT EnumVars ( IUnknown *pUnk, std::function<void(Var&)> fn )
{
    if ( pUnk == NULL ) return E_POINTER;

    Ptr<IEnumVARIANT> spEnum;
    HRESULT hr = pUnk->QueryInterface ( IID_IEnumVARIANT, (void**)spEnum.addr() );
    if ( FAILED(hr) ) return hr;

    for ( ;; )
    {
        VARIANT v; ::VariantInit ( &v );
        ULONG got = 0;
        hr = spEnum->Next ( 1, &v, &got );
        if ( FAILED(hr) || got == 0 ) { ::VariantClear ( &v ); break; }

        Var wrapped;
        ::VariantCopy ( wrapped.addr(), &v );
        fn ( wrapped );
        ::VariantClear ( &v );
    }
    return S_OK;
}

inline HRESULT EnumEach ( IUnknown *pUnk, std::function<void(Field&)> fn )
{
    if ( pUnk == NULL ) return E_POINTER;

    Ptr<IEnumVARIANT> spEnum;
    HRESULT hr = pUnk->QueryInterface ( IID_IEnumVARIANT, (void**)spEnum.addr() );
    if ( FAILED(hr) ) return hr;

    for ( ;; )
    {
        VARIANT v; ::VariantInit ( &v );
        ULONG got = 0;
        hr = spEnum->Next ( 1, &v, &got );
        if ( FAILED(hr) || got == 0 ) { ::VariantClear ( &v ); break; }

        if ( v.vt == VT_DISPATCH && v.pdispVal != NULL )
        {
            Field f;
            if ( SUCCEEDED ( v.pdispVal->QueryInterface ( IID_IMsgFieldCom, (void**)f.addr() ) ) )
                fn ( f );
        }
        ::VariantClear ( &v );
    }
    return S_OK;
}

inline HRESULT Field::forEach ( std::function<void(Field&)> fn ) const
{
    MSGC_IF_NULL(E_POINTER);
    Ptr<IUnknown> spUnk;
    HRESULT hr = m_p->get__NewEnum ( spUnk.addr() );
    if ( FAILED(hr) ) return hr;
    return EnumEach ( spUnk.get(), fn );
}

// ---------------------------------------------------------------------------
// StoreEventSink -- a hand-written IDispatch implementing the _IMsgStoreEvents
// dispinterface.
//
// This is the code an early-bound client has to write and a scripting client
// gets for free from its host. It is also exactly why a PowerShell client
// cannot sink these events without an interop assembly.
// ---------------------------------------------------------------------------
struct Change
{
    LONG         kind;              // one MsgTriggerFlag bit
    LONGLONG     p2pos;
    std::wstring path;              // resolved AT THE MUTATION, empty for a delete
};

typedef std::function<void(const Change&)>       ChangeHandler;
typedef std::function<void(const wchar_t *what)> ErrorHandler;

// ---------------------------------------------------------------------------
// PagingSink -- IMsgPagingSink, the SYNCHRONOUS counterpart of the event sink
// below, and the opposite of it in every respect that matters.
//
// OnChange is queued, replayed on a dispatch thread, free to block and free to
// re-enter the store. These three are called on the thread that provoked the
// access, while the core is BLOCKED waiting for the answer and the store lock
// is held. The return value is the answer: True means "the data is resident".
//
// It is a vtable implementation rather than an Invoke dispatcher because the
// server requires the sink to live in the registering apartment and calls it
// directly -- marshalling a call the core is synchronously waiting on, under a
// lock it already holds, is the deadlock the design exists to avoid.
// ---------------------------------------------------------------------------
class PagingSink : public IMsgPagingSink
{
  public:
    PagingSink ( ) : m_lRef ( 1 ), m_nIn ( 0 ), m_nOut ( 0 )
                   , m_bAnswer ( true ), m_nLastFlush ( -1 ) { }

    STDMETHOD(QueryInterface) ( REFIID riid, void **ppv )
    {
        if ( !ppv ) return E_POINTER;
        if ( ::IsEqualIID ( riid, IID_IUnknown ) ||
             ::IsEqualIID ( riid, IID_IDispatch ) ||
             ::IsEqualIID ( riid, IID_IMsgPagingSink ) )
        {
            *ppv = static_cast<IMsgPagingSink*>(this);
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
    STDMETHOD(Invoke) ( DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*,
                        EXCEPINFO*, UINT* ) { return E_NOTIMPL; }

    STDMETHOD(OnPageIn) ( VARIANT p2pos, VARIANT_BOOL *pHandled )
    {
        ++m_nIn; ::VariantCopy ( m_lastPos.addr(), &p2pos );
        if ( pHandled ) *pHandled = m_bAnswer ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    STDMETHOD(OnPageOut) ( VARIANT p2pos, VARIANT_BOOL flush, VARIANT_BOOL *pHandled )
    {
        ++m_nOut; ::VariantCopy ( m_lastPos.addr(), &p2pos ); m_nLastFlush = ( flush != VARIANT_FALSE ) ? 1 : 0;
        if ( pHandled ) *pHandled = m_bAnswer ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    // OnPopulate is gone. It was the core's third paging callback and is not
    // exposed below this tier, so there is nothing to raise it -- and a sink
    // that still declared it would have the wrong vtable, which is worse than
    // not having it.

    void  Reset ( )            { m_nIn = m_nOut = 0; m_nLastFlush = -1; }
    void  Answer ( bool b )    { m_bAnswer = b; }
    int   PageIns ( ) const    { return m_nIn; }
    int   PageOuts ( ) const   { return m_nOut; }
    int   LastFlush ( ) const  { return m_nLastFlush; }
    const Var& LastPos ( ) const { return m_lastPos; }

  private:
    LONG m_lRef;
    int  m_nIn, m_nOut;
    bool m_bAnswer;
    int  m_nLastFlush;
    Var  m_lastPos;
};

class StoreEventSink : public IDispatch
{
  public:
    StoreEventSink ( ) : m_lRef ( 1 ) { }

    STDMETHOD(QueryInterface) ( REFIID riid, void **ppv )
    {
        if ( !ppv ) return E_POINTER;
        if ( ::IsEqualIID ( riid, IID_IUnknown ) ||
             ::IsEqualIID ( riid, IID_IDispatch ) ||
             ::IsEqualIID ( riid, DIID__IMsgStoreEvents ) )
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
          case 1:       // OnChange(kind, p2pos, path) -- REVERSED in DISPPARAMS
          {
            if ( pdp->cArgs != 3 ) return DISP_E_BADPARAMCOUNT;
            Change c;
            c.path  = ( pdp->rgvarg[0].vt == VT_BSTR && pdp->rgvarg[0].bstrVal )
                    ? pdp->rgvarg[0].bstrVal : L"";
            c.p2pos = pdp->rgvarg[1].llVal;
            c.kind  = pdp->rgvarg[2].lVal;
            if ( onChange ) onChange ( c );
            break;
          }
          case 2:       // OnError(what)
            if ( onError && pdp->cArgs >= 1 && pdp->rgvarg[0].bstrVal )
                onError ( pdp->rgvarg[0].bstrVal );
            break;
          default:
            return DISP_E_MEMBERNOTFOUND;
        }
        return S_OK;
    }

    ChangeHandler onChange;
    ErrorHandler  onError;

  private:
    LONG m_lRef;
};

// ---------------------------------------------------------------------------
// Store -- CoCreateInstance(MsgcoreCom.MsgStore), plus its connection point.
//
// Each CoCreateInstance is its own DOCUMENT -- unlike TargetCom's P2PNetwork,
// which wraps a process-wide singleton -- so two of these never interfere.
// ---------------------------------------------------------------------------
class Store
{
  public:
    Store ( ) : m_pCP ( NULL ), m_pSink ( NULL ), m_dwCookie ( 0 )
    {
        m_hr = ::CoCreateInstance ( CLSID_MsgStore, NULL, CLSCTX_INPROC_SERVER,
                                    IID_IMsgStoreCom, (void**)m_sp.addr() );
    }
   ~Store ( ) { unadvise(); }

    Store             ( const Store& ) = delete;
    Store& operator = ( const Store& ) = delete;

    bool    ok ( ) const { return SUCCEEDED(m_hr) && m_sp.ok(); }
    HRESULT hr ( ) const { return m_hr; }
    IMsgStoreCom* raw ( ) const { return m_sp.get(); }

    // --- document -----------------------------------------------------------
    HRESULT open  ( LPCWSTR path ) { return m_sp->Open ( Bstr(path) ); }
    HRESULT save  ( LPCWSTR path = L"" ) { return m_sp->Save ( Bstr(path) ); }
    HRESULT close ( ) { return m_sp->Close(); }
    // Was nullify(). The store empties and stays usable, and -- unlike the old
    // method -- KEEPS ITS FILENAME, because it is the same store rather than a
    // rebuilt one.
    HRESULT clear ( ) { return m_sp->Clear(); }
    // Renames the ROOT NODE. The old renameFile() was a MoveFileEx on the
    // store's file, which this tier has no business doing: a host has its own
    // file API. This is the inverse of rootName().
    HRESULT renameRoot ( LPCWSTR newName ) { return m_sp->RenameRoot ( Bstr(newName) ); }
    HRESULT createNew ( LONG addrMode = ADDR_64, LONG initialBytes = 0, LONG maxBytes = 0 )
    { return m_sp->CreateNew ( addrMode, initialBytes, maxBytes ); }

    std::wstring filename ( ) const { BSTR bs = NULL; m_sp->get_Filename ( &bs ); return Take ( bs ); }
    std::wstring rootName ( ) const { BSTR bs = NULL; m_sp->get_RootName ( &bs ); return Take ( bs ); }
    bool dirty ( ) const { VARIANT_BOOL b = VARIANT_FALSE; m_sp->get_Dirty ( &b ); return b != VARIANT_FALSE; }
    HRESULT setDirty ( bool b ) { return m_sp->put_Dirty ( b ? VARIANT_TRUE : VARIANT_FALSE ); }
    LONG size ( ) const { LONG v = 0; m_sp->get_Size ( &v ); return v; }
    bool isValid ( ) const { VARIANT_BOOL b = VARIANT_FALSE; m_sp->get_IsValid ( &b ); return b != VARIANT_FALSE; }

    // --- navigation ---------------------------------------------------------
    Field root ( ) const { Field f; m_sp->get_Root ( f.addr() ); return f; }

    HRESULT fieldAt ( LPCWSTR dottedPath, Field& out ) const
    {
        Var v ( dottedPath );
        return m_sp->FieldAt ( v.raw(), out.addr() );
    }
    HRESULT fieldAt ( LONGLONG p2pos, Field& out ) const
    {
        Var v ( p2pos );
        return m_sp->FieldAt ( v.raw(), out.addr() );
    }
    Field fieldAt ( LPCWSTR dottedPath ) const { Field f; fieldAt ( dottedPath, f ); return f; }

    std::wstring pathOf ( LONGLONG p2pos ) const
    {
        BSTR bs = NULL;
        m_sp->PathOf ( p2pos, &bs );
        return Take ( bs );
    }

    // --- triggers -----------------------------------------------------------
    HRESULT armTrigger    ( LONG mask, LONGLONG p2pos ) { return m_sp->ArmTrigger ( mask, p2pos ); }
    HRESULT disarmTrigger ( LONG mask, LONGLONG p2pos ) { return m_sp->DisarmTrigger ( mask, p2pos ); }
    LONG fireTrigger ( LONG mask, LONGLONG p2pos )
    {
        LONG n = 0;
        m_sp->FireTrigger ( mask, p2pos, &n );
        return n;
    }

    // --- paging ---------------------------------------------------------------
    HRESULT setPagingSink ( IMsgPagingSink *p ) { return m_sp->SetPagingSink ( p ); }
    HRESULT pageIn ( LONGLONG p2pos, bool *pOk )
    {
        Var v ( p2pos );
        VARIANT_BOOL b = VARIANT_FALSE;
        HRESULT hr = m_sp->PageIn ( v.raw(), &b );
        if ( pOk ) *pOk = ( b != VARIANT_FALSE );
        return hr;
    }
    HRESULT pageOut ( LONGLONG p2pos, bool bFlush, bool *pOk )
    {
        Var v ( p2pos );
        VARIANT_BOOL b = VARIANT_FALSE;
        HRESULT hr = m_sp->PageOut ( v.raw(), bFlush ? VARIANT_TRUE : VARIANT_FALSE, &b );
        if ( pOk ) *pOk = ( b != VARIANT_FALSE );
        return hr;
    }
    // pageSumm is gone: it recounted a paged node after additions and removals,
    // and is not exposed below this tier.
    HRESULT pushPaging ( ) { return m_sp->PushPaging(); }
    HRESULT popPaging  ( ) { return m_sp->PopPaging(); }

    // --- utilities ----------------------------------------------------------
    std::wstring typeName ( LONG dataType ) const
    {
        BSTR bs = NULL;
        m_sp->TypeName ( dataType, &bs );
        return Take ( bs );
    }
    LONG typeFromName ( LPCWSTR n ) const
    {
        LONG v = -1;
        m_sp->TypeFromName ( Bstr(n), &v );
        return v;
    }
    // Ask BEFORE declaring, rather than declaring and reading the error. New at
    // this tier: 1 to 63 UTF-16 units and none of . @ : ^ / \ * ? | < > or ",
    // the first two because they are the path grammar's separators.
    bool isValidName ( LPCWSTR name ) const
    {
        VARIANT_BOOL b = VARIANT_FALSE;
        m_sp->IsValidName ( Bstr(name), &b );
        return b != VARIANT_FALSE;
    }
    bool wildcardMatch ( LPCWSTR pattern, LPCWSTR name ) const
    {
        VARIANT_BOOL b = VARIANT_FALSE;
        m_sp->WildcardMatch ( Bstr(pattern), Bstr(name), &b );
        return b != VARIANT_FALSE;
    }
    std::wstring versionString ( ) const
    {
        BSTR bs = NULL;
        m_sp->get_VersionString ( &bs );
        return Take ( bs );
    }

    // --- events -------------------------------------------------------------
    HRESULT advise ( )
    {
        unadvise();

        Ptr<IConnectionPointContainer> spCPC;
        HRESULT hr = m_sp->QueryInterface ( IID_IConnectionPointContainer, (void**)spCPC.addr() );
        if ( FAILED(hr) ) return hr;

        hr = spCPC->FindConnectionPoint ( DIID__IMsgStoreEvents, &m_pCP );
        if ( FAILED(hr) ) return hr;

        m_pSink = new StoreEventSink();
        return m_pCP->Advise ( m_pSink, &m_dwCookie );
    }

    StoreEventSink* sink ( ) const { return m_pSink; }

    void unadvise ( )
    {
        if ( m_pCP && m_dwCookie ) { m_pCP->Unadvise ( m_dwCookie ); m_dwCookie = 0; }
        if ( m_pSink ) { m_pSink->Release(); m_pSink = NULL; }
        if ( m_pCP )   { m_pCP->Release();   m_pCP = NULL; }
    }

  private:
    Ptr<IMsgStoreCom> m_sp;
    HRESULT           m_hr;
    IConnectionPoint *m_pCP;
    StoreEventSink   *m_pSink;
    DWORD             m_dwCookie;
};

// ---------------------------------------------------------------------------
// A scratch file path, so a harness that saves does not litter the tree.
// ---------------------------------------------------------------------------
inline std::wstring TempFile ( LPCWSTR leaf )
{
    WCHAR wszDir[MAX_PATH] = L".";
    ::GetTempPathW ( MAX_PATH, wszDir );
    std::wstring s ( wszDir );
    if ( !s.empty() && s[s.size()-1] != L'\\' ) s += L'\\';
    s += leaf;
    return s;
}

// Common failure exit: the server is not registered, or a dependency is missing.
inline int SetupFailure ( LPCWSTR what, HRESULT hr )
{
    Log ( L"MAIN", L"SETUP: %s failed (0x%08lX %s)", what, (unsigned long)hr, HrName ( hr ) );
    if ( hr == REGDB_E_CLASSNOTREG )
        wprintf ( L"\nMsgcoreCom is not registered. Run:\n"
                  L"    run_all.ps1            (registers per-user, runs, unregisters)\n"
                  L"or  regsvr32 /n /i:user \"...\\MsgFacade\\com\\out\\x64\\Debug\\MsgcoreCom.dll\"\n" );
    fflush ( stdout );
    return EXIT_SETUP;
}

} // namespace msgc
