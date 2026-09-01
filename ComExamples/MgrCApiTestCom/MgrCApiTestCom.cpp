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
// MgrCApiTestCom.cpp
//
// The counterpart of ..\DirectExamples\MgrCApiTest.
//
// That harness takes the store it already knows and drives it through a SECOND
// ABI -- the flat C one -- to show what a different calling convention does and
// does not preserve. This one does exactly the same thing, against the second
// ABI that exists HERE: every interface in MsgcoreCom is `dual`, so the same
// objects can be reached by vtable (what the other seven harnesses do) or
// LATE-BOUND through IDispatch::Invoke, which is the only way VBScript,
// PowerShell, VBA, classic ASP and most .NET hosts can reach them at all.
//
// Not one line below calls a vtable method of a MsgcoreCom interface. Every
// operation goes GetIDsOfNames -> Invoke, by NAME, exactly as a script engine
// does it -- so what passes here is what a script can do.
//
// FOUR THINGS ARE DIFFERENT THROUGH THIS FACE, and they are the point:
//
//   1. Failure arrives as DISP_E_EXCEPTION plus an EXCEPINFO, not as the
//      HRESULT itself. The msgc* code is in EXCEPINFO.scode -- which is what
//      VBScript's Err.Number and .NET's COMException.HResult read. Section 6.
//   2. A SUCCESS code other than S_OK does not survive. Invoke normalises it.
//      Section 6 measures it rather than asserting it.
//   3. DISPID_VALUE makes `field` read as its own value, and DISPID_NEWENUM
//      makes `For Each` work. Neither exists in the flat C ABI, because C has
//      no use for either. Section 3.
//   4. There is no type checking at the call site at all. A misspelled member
//      is DISP_E_MEMBERNOTFOUND at RUN TIME -- the price of late binding, and
//      the reason the type library is worth shipping. Section 1.
//
// Exit codes: 0 SUCCESS  1 SETUP  2 ASSERT  3 a check failed.

#include "../common/ComHarness.h"

using namespace msgc;


// ---------------------------------------------------------------------------
// Disp -- IDispatch by name, and nothing else.
//
// This is what a scripting host does for its user on every line. Written out
// once here so the harness body reads like the script it stands for.
// ---------------------------------------------------------------------------
class Disp
{
  public:
    Disp ( ) { }

    // Adopts a reference.
    void attach ( IDispatch *p ) { m_sp.attach ( p ); }

    // Takes any MsgcoreCom interface pointer and asks it for IDispatch --
    // which every one of them answers, because every one of them is `dual`.
    template <class T>
    HRESULT from ( T *p )
    {
        m_sp.release();
        if ( p == NULL ) return E_POINTER;
        return p->QueryInterface ( IID_IDispatch, (void**)m_sp.addr() );
    }

    bool       ok  ( ) const { return m_sp.ok(); }
    IDispatch* raw ( ) const { return m_sp.get(); }

    HRESULT dispidOf ( LPCWSTR name, DISPID *pId ) const
    {
        if ( !m_sp.ok() ) return E_POINTER;
        LPOLESTR p = (LPOLESTR)name;
        return m_sp->GetIDsOfNames ( IID_NULL, &p, 1, LOCALE_USER_DEFAULT, pId );
    }

    // One call. `wFlags` is DISPATCH_METHOD / _PROPERTYGET / _PROPERTYPUT.
    // Arguments are given in DECLARATION order and reversed here, because
    // DISPPARAMS carries them backwards -- the single most common mistake a
    // hand-written IDispatch client makes.
    HRESULT invoke ( LPCWSTR name, WORD wFlags,
                     const Var *args, UINT cArgs,
                     Var *pResult = NULL, EXCEPINFO *pEx = NULL ) const
    {
        if ( !m_sp.ok() ) return E_POINTER;

        DISPID dispid = 0;
        HRESULT hr = dispidOf ( name, &dispid );
        if ( FAILED(hr) ) return hr;

        return invokeId ( dispid, wFlags, args, cArgs, pResult, pEx );
    }

    HRESULT invokeId ( DISPID dispid, WORD wFlags,
                       const Var *args, UINT cArgs,
                       Var *pResult = NULL, EXCEPINFO *pEx = NULL ) const
    {
        if ( !m_sp.ok() ) return E_POINTER;

        std::vector<VARIANT> rev ( cArgs );
        for ( UINT i = 0; i < cArgs; ++i )
            rev[i] = args[cArgs - 1 - i].raw();      // borrowed, not owned

        DISPPARAMS dp;
        ::ZeroMemory ( &dp, sizeof(dp) );
        dp.rgvarg = cArgs ? &rev[0] : NULL;
        dp.cArgs  = cArgs;

        // A property PUT needs its value named DISPID_PROPERTYPUT, or the
        // engine has no way to tell the value from an index argument.
        DISPID putId = DISPID_PROPERTYPUT;
        if ( wFlags == DISPATCH_PROPERTYPUT )
        {
            dp.rgdispidNamedArgs = &putId;
            dp.cNamedArgs        = 1;
        }

        EXCEPINFO exLocal;
        ::ZeroMemory ( &exLocal, sizeof(exLocal) );
        EXCEPINFO *pUse = pEx ? pEx : &exLocal;
        ::ZeroMemory ( pUse, sizeof(EXCEPINFO) );

        VARIANT vRet; ::VariantInit ( &vRet );
        UINT uErr = 0;

        HRESULT hr = m_sp->Invoke ( dispid, IID_NULL, LOCALE_USER_DEFAULT, wFlags,
                                    &dp, pResult ? &vRet : NULL, pUse, &uErr );

        if ( pResult )
        {
            ::VariantCopy ( pResult->addr(), &vRet );
            ::VariantClear ( &vRet );
        }
        if ( pUse == &exLocal )
            ::SysFreeString ( exLocal.bstrDescription );

        return hr;
    }

    // The three shapes the harness uses constantly.
    HRESULT get ( LPCWSTR name, Var& out ) const
    { return invoke ( name, DISPATCH_PROPERTYGET, NULL, 0, &out ); }

    HRESULT put ( LPCWSTR name, const Var& v ) const
    { return invoke ( name, DISPATCH_PROPERTYPUT, &v, 1 ); }

    HRESULT call ( LPCWSTR name, const Var *args, UINT cArgs, Var *pOut = NULL,
                   EXCEPINFO *pEx = NULL ) const
    { return invoke ( name, DISPATCH_METHOD, args, cArgs, pOut, pEx ); }

    // A member that answers an OBJECT, wrapped as another Disp -- which is how
    // a script chains `store.Root.Child("a").Child("b")`.
    HRESULT object ( LPCWSTR name, WORD wFlags, const Var *args, UINT cArgs,
                     Disp& out, EXCEPINFO *pEx = NULL ) const
    {
        Var v;
        HRESULT hr = invoke ( name, wFlags, args, cArgs, &v, pEx );
        if ( FAILED(hr) ) return hr;
        if ( v.vt() != VT_DISPATCH || v.raw().pdispVal == NULL ) return E_NOINTERFACE;

        v.raw().pdispVal->AddRef();
        out.attach ( v.raw().pdispVal );
        return hr;
    }

  private:
    Ptr<IDispatch> m_sp;
};

// The failure a script sees: Invoke answers DISP_E_EXCEPTION and the real code
// and sentence are in the EXCEPINFO. This unpacks it the way a host does.
struct ScriptError
{
    HRESULT      hrInvoke;      // what Invoke returned
    HRESULT      scode;         // Err.Number / COMException.HResult
    std::wstring description;   // Err.Description / COMException.Message
};

static ScriptError CallExpectingFailure ( const Disp& d, LPCWSTR name, WORD wFlags,
                                          const Var *args, UINT cArgs )
{
    EXCEPINFO ex;
    ::ZeroMemory ( &ex, sizeof(ex) );

    ScriptError e;
    e.hrInvoke = d.invoke ( name, wFlags, args, cArgs, NULL, &ex );
    e.scode    = ex.scode ? ex.scode : (HRESULT)ex.wCode;
    if ( ex.bstrDescription ) e.description = ex.bstrDescription;

    ::SysFreeString ( ex.bstrSource );
    ::SysFreeString ( ex.bstrDescription );
    ::SysFreeString ( ex.bstrHelpFile );
    return e;
}

static void ShowScriptError ( LPCWSTR what, const ScriptError& e )
{
    std::wstring s = e.description;
    if ( s.size() > 150 ) { s.resize ( 147 ); s += L"..."; }
    wprintf ( L"  %s\n     Invoke -> 0x%08lX, Err.Number = 0x%08lX %s\n",
              what, (unsigned long)e.hrInvoke, (unsigned long)e.scode,
              HrName ( e.scode ) );
    if ( !s.empty() ) wprintf ( L"     Err.Description = %s\n", s.c_str() );
    fflush ( stdout );
}


// =========================================================================
// 1. Reaching the store at all, by name
// =========================================================================
static void Demo_LateBinding ( Disp& store )
{
    Section ( L"1. Late binding -- GetIDsOfNames and Invoke, nothing else" );

    // Every member is discoverable by NAME at run time. This is the whole of
    // what a script engine knows about the object before it calls it.
    const LPCWSTR members[] = { L"Root", L"Open", L"Save", L"Close", L"FieldAt",
                                L"RootName", L"Filename", L"Dirty", L"Size",
                                L"IsValid", L"PathOf", L"CreateNew", L"TypeName",
                                L"TypeFromName", L"WildcardMatch", L"VersionString",
                                L"ArmTrigger", L"FireTrigger",
                                // The three the move onto MsgFacade added, and
                                // they are discoverable like everything else --
                                // which is the whole test: a script sees a new
                                // member the moment the server has one.
                                L"Clear", L"RenameRoot", L"IsValidName" };
    for ( int i = 0; i < _countof(members); ++i )
    {
        DISPID id = 0;
        CHECK ( SUCCEEDED ( store.dispidOf ( members[i], &id ) ) );
    }

    // Names are matched case-INSENSITIVELY, which is what lets VBScript write
    // `store.root` and VBA write `Store.Root`.
    DISPID idLower = 0, idUpper = 0, idMixed = 0;
    CHECK ( SUCCEEDED ( store.dispidOf ( L"rootname", &idLower ) ) );
    CHECK ( SUCCEEDED ( store.dispidOf ( L"ROOTNAME", &idUpper ) ) );
    CHECK ( SUCCEEDED ( store.dispidOf ( L"RootName", &idMixed ) ) );
    CHECK ( idLower == idUpper && idUpper == idMixed );

    // AND A MISSPELLING IS A RUN-TIME ERROR, not a compile-time one. This is
    // the price of late binding, and the reason a type library is worth
    // shipping even to clients that will not use it for early binding.
    DISPID idBad = 0;
    CHECK ( store.dispidOf ( L"RootNmae", &idBad ) == DISP_E_UNKNOWNNAME );
    Note ( L"'RootNmae' -> DISP_E_UNKNOWNNAME at run time, not at build time" );

    Var v;
    CHECK ( SUCCEEDED ( store.get ( L"VersionString", v ) ) );
    CHECK ( !v.asText().empty() );
    Note ( L"VersionString: %s", v.asText().c_str() );

    CHECK ( SUCCEEDED ( store.get ( L"IsValid", v ) ) );
    CHECK ( v.asBool() );
}


// =========================================================================
// 2. Building a store through Invoke
// =========================================================================
static void Demo_BuildLateBound ( Disp& store )
{
    Section ( L"2. Declares and typed reads, all by name" );

    Disp root;
    CHECK ( SUCCEEDED ( store.object ( L"Root", DISPATCH_PROPERTYGET, NULL, 0, root ) ) );
    CHECK ( root.ok() );

    // Declare ( name, value, update ) -- three arguments in declaration order.
    // The first is a NAME, not a path: no leading separator, and a '.' in it
    // would be msgcName. FieldAt below is the one that takes a path.
    Var args[3];
    args[0] = Var ( L"cfg" );
    args[1] = Var ( 0 );
    args[2] = Var ( true );

    Disp cfg;
    CHECK ( SUCCEEDED ( root.object ( L"Declare", DISPATCH_METHOD, args, 3, cfg ) ) );
    CHECK ( cfg.ok() );

    struct { LPCWSTR name; Var value; LPCWSTR type; } leaves[] = {
        { L"width",   Var ( 1024 ),              L"INT32"  },
        { L"scale",   Var ( 1.5 ),               L"DOUBLE" },
        { L"title",   Var ( L"Chartboard" ),     L"WSTR16" },
        { L"visible", Var ( true ),              L"BOOL"   },
        { L"big",     Var ( (LONGLONG)5000000000LL ), L"INT64" },
    };

    for ( int i = 0; i < _countof(leaves); ++i )
    {
        Var a[3];
        a[0] = Var ( leaves[i].name );
        a[1] = leaves[i].value;
        a[2] = Var ( true );
        CHECK ( SUCCEEDED ( cfg.call ( L"Declare", a, 3 ) ) );
    }

    // Read them back, and check the DECLARED TYPE survived the trip through
    // two VARIANT conversions.
    for ( int i = 0; i < _countof(leaves); ++i )
    {
        Var a[1]; a[0] = Var ( leaves[i].name );
        Disp leaf;
        CHECK ( SUCCEEDED ( cfg.object ( L"Child", DISPATCH_METHOD, a, 1, leaf ) ) );

        Var t;
        CHECK ( SUCCEEDED ( leaf.get ( L"TypeName", t ) ) );
        CHECK ( t.asText() == leaves[i].type );
    }

    // DeclareTyped, the one member a VARIANT cannot stand in for.
    Var t4[4];
    t4[0] = Var ( L"tiny" );
    t4[1] = Var ( 250 );
    t4[2] = Var ( TYPE_UINT08 );
    t4[3] = Var ( true );
    CHECK ( SUCCEEDED ( cfg.call ( L"DeclareTyped", t4, 4 ) ) );

    Var a1[1]; a1[0] = Var ( L"tiny" );
    Disp tiny;
    CHECK ( SUCCEEDED ( cfg.object ( L"Child", DISPATCH_METHOD, a1, 1, tiny ) ) );
    Var tv;
    CHECK ( SUCCEEDED ( tiny.get ( L"TypeName", tv ) ) );
    CHECK ( tv.asText() == L"UINT08" );

    Var cnt;
    CHECK ( SUCCEEDED ( cfg.get ( L"Count", cnt ) ) );
    CHECK ( cnt.asLong() == 6 );
    Note ( L"6 children declared and typed, entirely through Invoke" );
}


// =========================================================================
// 3. DISPID_VALUE and DISPID_NEWENUM -- what C had no use for
// =========================================================================
//
// These two dispids are the whole reason a scripting client experiences this
// object as a VALUE WITH CHILDREN rather than as a handle with accessors. The
// flat C ABI has neither, and could not: C has nothing to spell them with.
//
static void Demo_DefaultMemberAndForEach ( Disp& store )
{
    Section ( L"3. DISPID_VALUE and DISPID_NEWENUM -- the script-shaped members" );

    Var pathArg[1]; pathArg[0] = Var ( L".cfg.width" );
    Disp width;
    CHECK ( SUCCEEDED ( store.object ( L"FieldAt", DISPATCH_METHOD, pathArg, 1, width ) ) );

    // `Value` is DISPID_VALUE (0), so these two are the SAME CALL -- which is
    // what makes `WScript.Echo store.FieldAt("cfg.width")` print 1024 rather
    // than a type name.
    Var byName, byDefault;
    CHECK ( SUCCEEDED ( width.get ( L"Value", byName ) ) );
    CHECK ( SUCCEEDED ( width.invokeId ( DISPID_VALUE, DISPATCH_PROPERTYGET, NULL, 0, &byDefault ) ) );
    CHECK ( byName.asLong() == 1024 );
    CHECK ( byDefault.asLong() == 1024 );

    DISPID idValue = 0;
    CHECK ( SUCCEEDED ( width.dispidOf ( L"Value", &idValue ) ) );
    CHECK ( idValue == DISPID_VALUE );

    // A property PUT through the default member, which is `f = 1600` in VB.
    CHECK ( SUCCEEDED ( width.invokeId ( DISPID_VALUE, DISPATCH_PROPERTYPUT,
                                         &Var ( 1600 ), 1 ) ) );
    CHECK ( SUCCEEDED ( width.get ( L"Value", byName ) ) );
    CHECK ( byName.asLong() == 1600 );

    // DISPID_NEWENUM (-4) is what `For Each child In cfg` compiles to.
    Var pathCfg[1]; pathCfg[0] = Var ( L".cfg" );
    Disp cfg;
    CHECK ( SUCCEEDED ( store.object ( L"FieldAt", DISPATCH_METHOD, pathCfg, 1, cfg ) ) );

    Var vEnum;
    CHECK ( SUCCEEDED ( cfg.invokeId ( DISPID_NEWENUM, DISPATCH_PROPERTYGET | DISPATCH_METHOD,
                                       NULL, 0, &vEnum ) ) );
    CHECK ( vEnum.vt() == VT_DISPATCH || vEnum.vt() == VT_UNKNOWN );

    IUnknown *pUnk = ( vEnum.vt() == VT_DISPATCH )
                   ? (IUnknown*)vEnum.raw().pdispVal
                   : vEnum.raw().punkVal;
    CHECK ( pUnk != NULL );

    Ptr<IEnumVARIANT> spEnum;
    CHECK ( pUnk != NULL && SUCCEEDED ( pUnk->QueryInterface ( IID_IEnumVARIANT, (void**)spEnum.addr() ) ) );

    // Walk it exactly as a host does, and read each element's DEFAULT member --
    // so this loop is `For Each c In cfg : total = total + c : Next`.
    int nSeen = 0;
    std::wstring names;
    if ( spEnum.ok() )
    {
        for ( ;; )
        {
            VARIANT v; ::VariantInit ( &v );
            ULONG got = 0;
            if ( FAILED ( spEnum->Next ( 1, &v, &got ) ) || got == 0 )
            { ::VariantClear ( &v ); break; }

            if ( v.vt == VT_DISPATCH && v.pdispVal != NULL )
            {
                Disp child;
                v.pdispVal->AddRef();
                child.attach ( v.pdispVal );

                Var nm;
                if ( SUCCEEDED ( child.get ( L"Name", nm ) ) )
                { names += nm.asText(); names += L" "; }
                ++nSeen;
            }
            ::VariantClear ( &v );
        }
    }
    CHECK ( nSeen == 6 );
    CHECK ( names == L"width scale title visible big tiny " );
    Note ( L"For Each over cfg: %d children -- %s", nSeen, names.c_str() );
}


// =========================================================================
// 4. The live/detached trap, and its removal
// =========================================================================
//
// THIS SECTION USED TO DEMONSTRATE A TRAP AND NOW DEMONSTRATES ITS ABSENCE, so
// it is worth saying what the trap was. Two families below this tier answered
// the same handle type: one aliased the live tree, the other deep-copied, and a
// write through the copy SUCCEEDED and reached nothing -- discovered later as a
// Save that did not contain the change. This server surfaced it as a readable
// IsLive and a trappable msgcDetached, which was the best that could be done
// from up here.
//
// It cannot arise now. Every node is a ROUTE re-resolved per call, so Child and
// Item and FieldAt all answer the same live thing, and Item / IsLive /
// msgcDetached are gone rather than kept as ceremony. What this section checks
// is that the trap is unreachable: the removed members are NOT FOUND by name,
// which is exactly how a script would discover it.
//
static void Demo_NoDetachedNodes ( Disp& store )
{
    Section ( L"4. Live vs detached -- the trap is gone, not merely trappable" );

    Var pathCfg[1]; pathCfg[0] = Var ( L".cfg" );
    Disp cfg;
    CHECK ( SUCCEEDED ( store.object ( L"FieldAt", DISPATCH_METHOD, pathCfg, 1, cfg ) ) );

    Var nm[1]; nm[0] = Var ( L"title" );

    Disp byChild;
    CHECK ( SUCCEEDED ( cfg.object ( L"Child", DISPATCH_METHOD, nm, 1, byChild ) ) );

    // The two members the trap was made of are simply not there, and a script
    // finds that out the same way it finds out about a typo.
    DISPID idGone = 0;
    CHECK ( byChild.dispidOf ( L"Item",   &idGone ) == DISP_E_UNKNOWNNAME );
    CHECK ( byChild.dispidOf ( L"IsLive", &idGone ) == DISP_E_UNKNOWNNAME );
    Note ( L"Field.Item and Field.IsLive: DISP_E_UNKNOWNNAME -- both removed" );

    // The write reaches the store, as it does through every node here.
    CHECK ( SUCCEEDED ( byChild.put ( L"Value", Var ( L"Chartboard II" ) ) ) );
    Var v;
    CHECK ( SUCCEEDED ( byChild.get ( L"Value", v ) ) );
    CHECK ( v.asText() == L"Chartboard II" );

    // The SECOND route to the same node -- through the descendant collection,
    // whose Item was the other detached accessor -- reads the write and can make
    // one of its own.
    Disp desc;
    Var create[1]; create[0] = Var ( true );
    CHECK ( SUCCEEDED ( cfg.object ( L"Descendants", DISPATCH_METHOD, create, 1, desc ) ) );

    Disp viaColl;
    CHECK ( SUCCEEDED ( desc.object ( L"Item", DISPATCH_PROPERTYGET, nm, 1, viaColl ) ) );
    CHECK ( SUCCEEDED ( viaColl.get ( L"Value", v ) ) );
    CHECK ( v.asText() == L"Chartboard II" );

    CHECK ( SUCCEEDED ( viaColl.put ( L"Value", Var ( L"Chartboard III" ) ) ) );

    Disp reread;
    CHECK ( SUCCEEDED ( cfg.object ( L"Child", DISPATCH_METHOD, nm, 1, reread ) ) );
    CHECK ( SUCCEEDED ( reread.get ( L"Value", v ) ) );
    CHECK ( v.asText() == L"Chartboard III" );      // the collection write landed

    // A FAILURE A SCRIPT CAN STILL TRAP, so the mechanism this section used to
    // exercise is not lost with the trap: a bad name is msgcName, arriving as
    // DISP_E_EXCEPTION with an scode and a sentence in EXCEPINFO.
    ScriptError e = CallExpectingFailure ( cfg, L"RenameChild", DISPATCH_METHOD,
                                           NULL, 0 );
    CHECK ( e.hrInvoke != S_OK );
    ShowScriptError ( L"RenameChild with no arguments", e );
}


// =========================================================================
// 5. P2Pos, paths, save and load -- late-bound
// =========================================================================
static void Demo_PathsAndPersistence ( Disp& store )
{
    Section ( L"5. P2Pos, paths, Save / Load -- by name" );

    Var pathArg[1]; pathArg[0] = Var ( L".cfg.width" );
    Disp width;
    CHECK ( SUCCEEDED ( store.object ( L"FieldAt", DISPATCH_METHOD, pathArg, 1, width ) ) );

    Var vPos;
    CHECK ( SUCCEEDED ( width.get ( L"P2Pos", vPos ) ) );
    CHECK ( vPos.asInt64() != 0 );

    // A hyper crosses as VT_I8, which .NET, PowerShell and 64-bit VBA read
    // directly. (A host that cannot -- VBScript, 32-bit VBA -- uses Path, which
    // is why both identities are published.)
    CHECK ( vPos.vt() == VT_I8 );

    Var vPath;
    CHECK ( SUCCEEDED ( width.get ( L"Path", vPath ) ) );
    CHECK ( vPath.asText() == L".cfg.width" );

    // PathOf AND Path ARE THE SAME SPELLING NOW. They were not: PathOf answered
    // the kernel's own grammar, with the root's NAME as its first segment, and
    // feeding it back to FieldAt found nothing. A script that logged one and
    // resolved the other got a silent miss.
    Var posArg[1]; posArg[0] = Var ( vPos.asInt64() );
    Var vOwn;
    CHECK ( SUCCEEDED ( store.call ( L"PathOf", posArg, 1, &vOwn ) ) );
    CHECK ( vOwn.asText() == vPath.asText() );
    Note ( L"Path='%s'  PathOf='%s'  -- one spelling, and FieldAt takes it",
           vPath.asText().c_str(), vOwn.asText().c_str() );

    // FieldAt takes EITHER identity, and both answer the same kind of object:
    // a writable node.
    Disp byPos;
    CHECK ( SUCCEEDED ( store.object ( L"FieldAt", DISPATCH_METHOD, posArg, 1, byPos ) ) );
    Var vp;
    CHECK ( SUCCEEDED ( byPos.get ( L"Path", vp ) ) );
    CHECK ( vp.asText() == vPath.asText() );
    CHECK ( SUCCEEDED ( byPos.put ( L"Value", Var ( 1600 ) ) ) );

    // Save and reopen, all by name.
    const std::wstring file = TempFile ( L"MgrCApiTestCom.p2p" );
    ::DeleteFileW ( file.c_str() );

    Var fileArg[1]; fileArg[0] = Var ( file.c_str() );
    CHECK ( SUCCEEDED ( store.call ( L"Save", fileArg, 1 ) ) );

    Var vFile;
    CHECK ( SUCCEEDED ( store.get ( L"Filename", vFile ) ) );
    CHECK ( vFile.asText() == file );

    Ptr<IMsgStoreCom> spOther;
    CHECK ( SUCCEEDED ( ::CoCreateInstance ( CLSID_MsgStore, NULL, CLSCTX_INPROC_SERVER,
                                             IID_IMsgStoreCom, (void**)spOther.addr() ) ) );
    Disp other;
    CHECK ( SUCCEEDED ( other.from ( spOther.get() ) ) );
    CHECK ( SUCCEEDED ( other.call ( L"Open", fileArg, 1 ) ) );

    Disp w2;
    CHECK ( SUCCEEDED ( other.object ( L"FieldAt", DISPATCH_METHOD, pathArg, 1, w2 ) ) );
    Var v2;
    CHECK ( SUCCEEDED ( w2.get ( L"Value", v2 ) ) );
    CHECK ( v2.asLong() == 1600 );

    Disp tiny2;
    Var tinyPath[1]; tinyPath[0] = Var ( L".cfg.tiny" );
    CHECK ( SUCCEEDED ( other.object ( L"FieldAt", DISPATCH_METHOD, tinyPath, 1, tiny2 ) ) );
    Var t2;
    CHECK ( SUCCEEDED ( tiny2.get ( L"TypeName", t2 ) ) );
    CHECK ( t2.asText() == L"UINT08" );             // the width survived

    // Rename / move / retype, the three structural edits, late-bound.
    Disp cfg2;
    Var cfgPath[1]; cfgPath[0] = Var ( L".cfg" );
    CHECK ( SUCCEEDED ( other.object ( L"FieldAt", DISPATCH_METHOD, cfgPath, 1, cfg2 ) ) );

    Var ren[2]; ren[0] = Var ( L"title" ); ren[1] = Var ( L"caption" );
    Var vRen;
    CHECK ( SUCCEEDED ( cfg2.call ( L"RenameChild", ren, 2, &vRen ) ) );
    CHECK ( vRen.asBool() == true );

    Var ex[1]; ex[0] = Var ( L"caption" );
    Var vEx;
    CHECK ( SUCCEEDED ( cfg2.call ( L"Exists", ex, 1, &vEx ) ) );
    CHECK ( vEx.asBool() == true );

    // RetypeChild takes a TYPE now, not a value. It used to take a VARIANT --
    // "sixteen hundred" here -- because the retype entry points below were one
    // per type and a value was the only way to pick one. A retype seeds a ZERO
    // by definition, so that argument was always a fiction.
    Var ret[2]; ret[0] = Var ( L"width" ); ret[1] = Var ( (LONG)26 );  // WSTR16
    Var vRet;
    CHECK ( SUCCEEDED ( cfg2.call ( L"RetypeChild", ret, 2, &vRet ) ) );
    CHECK ( vRet.asBool() == true );

    Disp w3;
    Var wn[1]; wn[0] = Var ( L"width" );
    CHECK ( SUCCEEDED ( cfg2.object ( L"Child", DISPATCH_METHOD, wn, 1, w3 ) ) );
    Var t3;
    CHECK ( SUCCEEDED ( w3.get ( L"TypeName", t3 ) ) );
    CHECK ( t3.asText() == L"WSTR16" );

    CHECK ( SUCCEEDED ( other.call ( L"Close", NULL, 0 ) ) );
    ::DeleteFileW ( file.c_str() );
}


// =========================================================================
// 6. What automation does to an HRESULT
// =========================================================================
//
// This is the section that corresponds to MgrCApiTest's finding that a type
// mismatch through the flat C API is UNRECOVERABLE -- the wrappers throw out of
// an extern "C" function and MSVC deletes the handler. The equivalent question
// here is what survives Invoke, and the answer is much better in one direction
// and lossy in the other.
//
static void Demo_ErrorsThroughInvoke ( Disp& store )
{
    Section ( L"6. What survives Invoke -- EXCEPINFO in, success codes out" );

    Disp root;
    CHECK ( SUCCEEDED ( store.object ( L"Root", DISPATCH_PROPERTYGET, NULL, 0, root ) ) );

    // FAILURES SURVIVE, AND SURVIVE WELL. Every one arrives as
    // DISP_E_EXCEPTION with the msgc* code in EXCEPINFO.scode and the sentence
    // in bstrDescription -- Err.Number and Err.Description exactly.
    struct { LPCWSTR what; LPCWSTR member; WORD flags; Var a0; Var a1; UINT n; HRESULT want; } cases[] = {
        { L"Child of a name that is not there", L"Child",   DISPATCH_METHOD,
          Var ( L"nosuch" ),  Var(), 1, E_NO_FIELD },
        { L"Declare with an over-long name",    L"Declare", DISPATCH_METHOD,
          Var ( std::wstring ( 64, L'z' ).c_str() ), Var ( 1 ), 2, E_NAME },
        { L"Declare with an Empty value",       L"Declare", DISPATCH_METHOD,
          Var ( L"nothing" ), Var(), 2, E_TYPE },
        { L"List on a node that is not one",    L"List",    DISPATCH_PROPERTYGET,
          Var(), Var(), 0, E_NOT_LIST },
    };

    for ( int i = 0; i < _countof(cases); ++i )
    {
        Var args[2]; args[0] = cases[i].a0; args[1] = cases[i].a1;
        ScriptError e = CallExpectingFailure ( root, cases[i].member, cases[i].flags,
                                               args, cases[i].n );
        CHECK ( e.hrInvoke == DISP_E_EXCEPTION );
        CHECK ( e.scode == cases[i].want );
        CHECK ( !e.description.empty() );
        ShowScriptError ( cases[i].what, e );
    }

    // A SUCCESS CODE OTHER THAN S_OK DOES NOT SURVIVE, and this measures it
    // rather than asserting it. Cursor.Next answers S_FALSE by vtable when it
    // steps off the end; through Invoke a host sees plain success and must ask
    // EndOfCursor instead. Same shape as TargetCom's P2PF_S_UNRELATED_LINK,
    // and the same remedy: publish the answer as a readable property.
    Var cfgPath[1]; cfgPath[0] = Var ( L".cfg" );
    Disp cfg;
    CHECK ( SUCCEEDED ( store.object ( L"FieldAt", DISPATCH_METHOD, cfgPath, 1, cfg ) ) );

    Disp curs;
    CHECK ( SUCCEEDED ( cfg.object ( L"Cursor", DISPATCH_PROPERTYGET, NULL, 0, curs ) ) );

    Var vCount;
    CHECK ( SUCCEEDED ( curs.get ( L"Count", vCount ) ) );
    CHECK ( vCount.asLong() == 6 );

    CHECK ( SUCCEEDED ( curs.call ( L"Seek", NULL, 0 ) ) );

    // Step past the last element. The LAST of these returns S_FALSE by vtable.
    HRESULT hrLast = S_OK;
    for ( int i = 0; i < 6; ++i )
        hrLast = curs.call ( L"Next", NULL, 0 );

    CHECK ( SUCCEEDED ( hrLast ) );
    CHECK ( hrLast == S_OK );                       // normalised: NOT S_FALSE
    Note ( L"Cursor.Next past the end: vtable says S_FALSE, Invoke says 0x%08lX",
           (unsigned long)hrLast );

    // Which is exactly why the state is also a PROPERTY. This is the line a
    // script has to write instead, and it is not worse -- just different.
    Var vEoc;
    CHECK ( SUCCEEDED ( curs.get ( L"EndOfCursor", vEoc ) ) );
    CHECK ( vEoc.asBool() == true );

    Var vIdx;
    CHECK ( SUCCEEDED ( curs.get ( L"Index", vIdx ) ) );
    CHECK ( vIdx.asLong() == -1 );

    // ISupportErrorInfo is what tells a host the EXCEPINFO above is
    // trustworthy for this interface -- without it, a cautious host ignores
    // the error object.
    Ptr<ISupportErrorInfo> spSEI;
    CHECK ( SUCCEEDED ( root.raw()->QueryInterface ( IID_ISupportErrorInfo,
                                                     (void**)spSEI.addr() ) ) );
    CHECK ( spSEI.ok() && spSEI->InterfaceSupportsErrorInfo ( IID_IMsgFieldCom ) == S_OK );
    Note ( L"ISupportErrorInfo confirms IMsgFieldCom raises rich errors" );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    InitConsole();

    wprintf ( L"=== MgrCApiTestCom - the same store through IDispatch late binding ===\n" );
    fflush ( stdout );

    Apartment apt;
    if ( !apt.ok() ) return SetupFailure ( L"CoInitializeEx", E_FAIL );

    Ptr<IMsgStoreCom> spStore;
    HRESULT hr = ::CoCreateInstance ( CLSID_MsgStore, NULL, CLSCTX_INPROC_SERVER,
                                      IID_IMsgStoreCom, (void**)spStore.addr() );
    if ( FAILED(hr) ) return SetupFailure ( L"CoCreateInstance(MsgcoreCom.MsgStore)", hr );

    Disp store;
    if ( FAILED ( store.from ( spStore.get() ) ) )
        return SetupFailure ( L"QueryInterface(IDispatch)", E_NOINTERFACE );

    Demo_LateBinding            ( store );
    Demo_BuildLateBound         ( store );
    Demo_DefaultMemberAndForEach( store );
    Demo_NoDetachedNodes        ( store );
    Demo_PathsAndPersistence    ( store );
    Demo_ErrorsThroughInvoke    ( store );

    return Verdict();
}
