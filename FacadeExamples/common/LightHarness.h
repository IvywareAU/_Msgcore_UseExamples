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
// LightHarness.h
//
// The small amount of scaffolding the Light harnesses still need, once
// MsgFacade has absorbed everything else.
//
// What ISN'T here is the point of this directory. Each original in
// ..\DirectExamples carried, per file:
//
//   * a stdafx.h pulling in afx.h/afxwin.h/afxext.h/afxtempl.h and the
//     Msgcore headers (P2Pmsg.h, MsgDesc.h, MsgCurs.h, P2PmsgMgr.h, ...);
//   * `CWinApp theApp;` -- MFC's one-instance-per-exe rule, which a console
//     harness inherits only because Msgcore's headers use CString and CList;
//   * a _CrtSetReportHook assert trap, because a debug ASSERT inside the
//     kernel would otherwise pop a MODAL DIALOG and hang a headless run;
//   * a `catch (P2Pevent*) { ...->Cancel(false); }` around main, because
//     Msgcore reports failure by throwing a POINTER that the catcher must
//     then dispose of.
//
// None of that survives the facade. There is no MFC in these processes, so
// there is no CWinApp and no modal assert to trap; every facade entry point
// catches whatever the kernel raises and answers an HRESULT, so the only
// exception a client can see is the one MsgFacadeFn.hpp raises on its behalf
// (msgf::Error, carrying that HRESULT). So all that is left is: count checks,
// name the failures, and report a verdict.
//
// EXIT-CODE CONTRACT, kept identical to the originals so the two trees are
// directly comparable:
//   0 = SUCCESS : every check passed.
//   1 = SETUP   : the library/store could not be created at all.
//   3 = FAIL    : at least one check failed (or, for the two mesh harnesses,
//                 the traffic never arrived).
//   2 = was "an MFC/CRT assertion fired". It is now unreachable BY
//       CONSTRUCTION -- there is no MFC linked into any of these executables.

#pragma once

#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <string>

#include "MsgFacade.h"

namespace light {

const int EXIT_SUCCESS_ = 0;
const int EXIT_SETUP    = 1;
const int EXIT_FAIL     = 3;

// ---------------------------------------------------------------------------
// Console
//
// UTF-16 mode so wprintf emits Unicode correctly, and UNBUFFERED so that if a
// harness ever does die mid-way the last line printed is the one that says
// where. (MsgFacade's own smoke test does the same, for the same reason.)
// ---------------------------------------------------------------------------
inline void InitConsole ( )
{
    _setmode ( _fileno ( stdout ), _O_U16TEXT );
    ::setvbuf ( stdout, NULL, _IONBF, 0 );
}

// ---------------------------------------------------------------------------
// The check counters
// ---------------------------------------------------------------------------
inline int& Checks ( ) { static int n = 0; return n; }
inline int& Failed ( ) { static int n = 0; return n; }

inline void Check ( bool bOk, const wchar_t *lpszWhat, int nLine )
{
    ++Checks ( );
    if ( bOk ) return;
    ++Failed ( );
    wprintf ( L"  FAIL (line %d): %s\n", nLine, lpszWhat );
}

inline void Section ( const wchar_t *lpszTitle )
{
    wprintf ( L"\n--- %s\n", lpszTitle );
}

// ---------------------------------------------------------------------------
// Facade HRESULT names.
//
// The originals had no equivalent: Msgcore's failures were exceptions whose
// only description was a formatted CString. Here every failure has a NUMBER
// with a stable meaning, so a harness can assert the SPECIFIC failure rather
// than merely "it threw". Several checks below do exactly that, and that is
// the single largest behavioural difference between the two trees.
// ---------------------------------------------------------------------------
inline const wchar_t* HrName ( HRESULT hr )
{
    if ( hr == S_OK )                      return L"S_OK";
    if ( hr == S_FALSE )                   return L"S_FALSE";
    if ( hr == E_POINTER )                 return L"E_POINTER";
    if ( hr == E_INVALIDARG )              return L"E_INVALIDARG";
    if ( hr == HRESULT_FROM_WIN32 ( ERROR_MORE_DATA ) ) return L"ERROR_MORE_DATA";
    if ( hr == msgf::MSGF_E_ABI_MISMATCH ) return L"MSGF_E_ABI_MISMATCH";
    if ( hr == msgf::MSGF_E_CORE )         return L"MSGF_E_CORE";
    if ( hr == msgf::MSGF_E_NO_ITEM )      return L"MSGF_E_NO_ITEM";
    if ( hr == msgf::MSGF_E_NAME )         return L"MSGF_E_NAME";
    if ( hr == msgf::MSGF_E_TYPE )         return L"MSGF_E_TYPE";
    if ( hr == msgf::MSGF_E_RANGE )        return L"MSGF_E_RANGE";
    if ( hr == msgf::MSGF_E_FILE )         return L"MSGF_E_FILE";
    if ( hr == msgf::MSGF_E_CLOSED )       return L"MSGF_E_CLOSED";
    if ( hr == msgf::MSGF_E_SCOPE )        return L"MSGF_E_SCOPE";
    if ( hr == msgf::MSGF_E_PATH )         return L"MSGF_E_PATH";
    if ( hr == msgf::MSGF_E_EXISTS )       return L"MSGF_E_EXISTS";
    if ( hr == msgf::MSGF_E_NO_POS )       return L"MSGF_E_NO_POS";
    if ( hr == msgf::MSGF_E_LIMIT )        return L"MSGF_E_LIMIT";
    if ( hr == msgf::MSGF_E_DEPTH )        return L"MSGF_E_DEPTH";
    return L"(other)";
}

inline void CheckHr ( HRESULT hr, HRESULT hrWant, const wchar_t *lpszWhat, int nLine )
{
    ++Checks ( );
    if ( hr == hrWant ) return;
    ++Failed ( );
    wprintf ( L"  FAIL (line %d): %s -- got %s, wanted %s\n"
            , nLine, lpszWhat, HrName ( hr ), HrName ( hrWant ) );
}

// ---------------------------------------------------------------------------
// A scratch file under the OS temp area, unique to this process so two
// harnesses running side by side cannot collide.
// ---------------------------------------------------------------------------
inline std::wstring TempPath ( const wchar_t *lpszLeaf )
{
    wchar_t szDir[MAX_PATH] = { 0 };
    ::GetTempPathW ( MAX_PATH, szDir );

    wchar_t szOut[MAX_PATH] = { 0 };
    ::swprintf_s ( szOut, MAX_PATH, L"%s%s_%lu.p2p"
                 , szDir, lpszLeaf, ::GetCurrentProcessId ( ) );
    return std::wstring ( szOut );
}

// ---------------------------------------------------------------------------
// The verdict, in the originals' words.
// ---------------------------------------------------------------------------
inline int Verdict ( )
{
    const int nExit = ( Failed ( ) == 0 ) ? EXIT_SUCCESS_ : EXIT_FAIL;
    wprintf ( L"\n%d checks, %d failed. Done (exit=%d).\n"
            , Checks ( ), Failed ( ), nExit );
    return nExit;
}

} // namespace light

// The two macros every harness uses. CHECK stringises its own expression, so a
// failure names the condition; CHECK_HR names the two codes instead, which is
// the more useful report when the point of the check IS the error code.
#define CHECK(expr)          light::Check   ( (expr), L#expr, __LINE__ )
#define CHECK_HR(hr, want)   light::CheckHr ( (hr), (want), L#hr, __LINE__ )
