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
// WsaQueryTest.cpp
//
// A Msgcore store used as a SERVICE: one hub owns a P2PmsgMgr catalogue, the
// other queries it by path over a loopback TCP socket, and each answer
// carries the value AND its Msgcore type tag. Two hubs, one process.
//
// WsaStoreTest shipped a whole store as one opaque blob. This is the other
// shape, and the more usual one: the store STAYS on the server, and only
// small typed answers cross the wire. That makes the store the authority and
// the messages a query protocol over it.
//
// What each library contributes:
//
//   Msgcore     the catalogue, the path lookup (RootPath2Object), the type
//               tag that makes an answer self-describing, and P2Pos as the
//               stable handle the server hands back as a row id
//   Targetcore  named messages routed by BEGIN_P2PeerMsg_MAP -- the client
//               posts "StoreQuery", the server replies "StoreReply", and
//               each end only ever sees the messages addressed to it
//
// THE PROTOCOL is deliberately trivial, because the interesting part is what
// is behind it, not the encoding:
//
//   StoreQuery   payload = the query path, a NUL-terminated wide string
//   StoreReply   payload = "path|TYPE|value|p2pos", same encoding
//
// A real protocol would carry a serialised Msgcore field rather than a
// delimited string -- see WsaStoreTest for how a Msgcore image becomes a
// payload. This one stays readable so the routing is what you notice.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS : every query answered, and every answer matched.
//   1 = SETUP   : startup/factory failure.
//   2 = ASSERT  : an MFC/CRT assertion fired.
//   3 = TIMEOUT : not all replies arrived in time, or an answer was wrong.

#include "stdafx.h"
#include "WsaQueryTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pwin32.h"
#include "P2PeerHub.h"
#include "P2PeerConWsa.h"
#include "P2PeerMsg.h"

#include "P2Pmsg.h"
#include "MsgDesc.h"
#include "P2PmsgMgr.h"
#include "Msgexception.h"

#include <string>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CWinApp theApp;

// -------------------------------------------------------------------------
// Mesh configuration
// -------------------------------------------------------------------------
static const short      kTestPort   = 7812;
static const P2PaddrSTR kServerAddr = L"MsgQuery.Server";
static const P2PaddrSTR kClientAddr = L"MsgQuery.Client";

// Application message names. The SAME literal appears in ON_P2PeerMsg and in
// the P2PeerMsg32 constructor -- routing is by name, matched as a string.
#define kMsgQuery  L"StoreQuery"
#define kMsgReply  L"StoreReply"

static HANDLE g_hDoneEvent = NULL;

// The queries the client will issue, and what each answer must be. Paths use
// the form P2Pos2Path renders: dot-separated, rooted at the manager.
struct QuerySpec
{
    LPCWSTR lpszPath;
    LPCWSTR lpszType;      // expected Msgcore type tag
    LPCWSTR lpszValue;     // expected rendered value
};
static const QuerySpec kQueries[] = {
    { L"..Product",            L"WSTR16", L"Chartboard"  },
    { L"..Release.Major",      L"int32",  L"7"           },
    { L"..Release.Minor",      L"int32",  L"12"          },
    { L"..Limits.MaxSeries",   L"int32",  L"512"         },
    { L"..Missing.Node",       L"",       L""            },   // must MISS cleanly
};
static const int kQueryCount = (int)(sizeof(kQueries) / sizeof(kQueries[0]));

// Written by the CLIENT hub thread, read by main after the done event.
static volatile LONG g_nChecks   = 0;
static volatile LONG g_nFailed   = 0;
static volatile LONG g_nAnswered = 0;

static void Check(bool bOk, LPCWSTR lpszWhat, int nLine)
{
    InterlockedIncrement(&g_nChecks);
    if (bOk) return;
    InterlockedIncrement(&g_nFailed);
    wprintf(L"  FAIL (line %d): %s\n", nLine, lpszWhat);
    fflush(stdout);
}
#define CHECK(expr) Check((expr), L#expr, __LINE__)

static void LogAt(LPCWSTR lpszRole, LPCWSTR lpszMsg)
{
    SYSTEMTIME st; GetLocalTime(&st);
    wprintf(L"[%02d:%02d:%02d.%03d tid=%lu %s] %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), lpszRole, lpszMsg);
    fflush(stdout);
}

static int __cdecl AssertReportHook(int nReportType, char* szMsg, int* pnRet)
{
    if (nReportType == _CRT_ASSERT)
    {
        fflush(stdout);
        fprintf(stderr, "\n=== ASSERT TRIPPED ===\n%s\n", szMsg ? szMsg : "(no message)");
        fflush(stderr);
        if (pnRet) *pnRet = 0;
        ExitProcess(2);
    }
    return FALSE;
}

// -------------------------------------------------------------------------
// Payload helpers -- a wide string in, a wide string out.
// -------------------------------------------------------------------------
static P2Psize_t WideBytes(LPCWSTR lpsz)
{
    return (P2Psize_t)((wcslen(lpsz) + 1) * sizeof(wchar_t));
}

static std::wstring PayloadText(P2PeerMsg* pMsg)
{
    if (!pMsg || !pMsg->Data() || pMsg->DataSize() == 0) return std::wstring();
    return std::wstring((LPCWSTR)pMsg->Data());
}

// Split "a|b|c|d" into its fields.
static void SplitPipe(const std::wstring& str, std::wstring aOut[], int nMax)
{
    int    n     = 0;
    size_t nFrom = 0;
    while (n < nMax)
    {
        size_t nBar = str.find(L'|', nFrom);
        if (nBar == std::wstring::npos) { aOut[n++] = str.substr(nFrom); break; }
        aOut[n++] = str.substr(nFrom, nBar - nFrom);
        nFrom = nBar + 1;
    }
    while (n < nMax) aOut[n++].clear();
}


// =========================================================================
// QueryHub : the service on one side, the caller on the other
// =========================================================================
class QueryHub : public P2PeerHub
{
    DECLARE_P2PeerMsg_MAP()

public:
    QueryHub(P2PaddrSTR strAddr, bool bServer)
        : P2PeerHub(strAddr)
        , m_bServer(bServer)
        , m_bSent(false)
        , m_pStore(nullptr)
    {
        if (m_bServer) BuildCatalogue();
    }

    virtual ~QueryHub()
    {
        delete m_pStore;
        m_pStore = nullptr;
    }

protected:
    // ---- SERVER: answer a query out of the store ------------------------
    //
    // Runs on the server hub's pump thread. The store is touched only from
    // here, so it needs no lock: one hub, one pump, one thread (see
    // _Targetcore_UseExamples\ArchitectureFAQ.md Q3-Q6). Give the store a second
    // reader and that stops being true.
    //
    msgRESULT On_StoreQuery(P2PeerMsg* pMsg)
    {
        const std::wstring strPath = PayloadText(pMsg);
        std::wstring strReply;

        if (!m_pStore)
        {
            strReply = strPath + L"|ERR|no store|0";
        }
        else
        {
            try
            {
                // RootPath2Object resolves the dotted path against the store.
                P3PmsgField oField(m_pStore->RootPath2Object(strPath.c_str()));

                // The answer is self-describing: the type tag travels with the
                // value, so the caller never has to guess how to read it, and
                // the P2Pos gives it a stable handle to ask again later.
                wchar_t szOut[512];
                swprintf_s(szOut, 512, L"%s|%s|%s|%llu",
                           strPath.c_str(),
                           oField.ToStringType(),
                           oField.ToString(),
                           (unsigned long long)oField.GetP2Pos());
                strReply = szOut;
            }
            catch (P2Pevent* pEVT)
            {
                // A MISS IS AN EXCEPTION, NOT AN EMPTY RESULT. RootPath2Object
                // does not return a null object for an unknown path -- it
                // throws a P2Pevent carrying "Path to object does not exist".
                // Any lookup service over a Msgcore store therefore has to
                // convert that throw into an answer, which is what this does.
                //
                // Doing so is also mandatory for a different reason: an
                // exception escaping a hub handler is caught by the pump,
                // which then DROPS the connection (P2Pwin32.cpp:3279). A
                // client asking for a name that does not exist would silently
                // lose the link.
                const CString strMsg = pEVT ? pEVT->GetMessage() : CString(L"<null>");
                wprintf(L"[SERVER]   miss: %s\n", (LPCWSTR)strMsg);
                strReply = strPath + L"|MISS||0";
                if (pEVT) pEVT->Cancel(false);
            }
        }

        wprintf(L"[SERVER] '%s' -> '%s'\n", strPath.c_str(), strReply.c_str());
        fflush(stdout);

        // Reply source = this hub; destination = whoever asked.
        PostP2PeerMsg(new P2PeerMsg32(
            kServerAddr, pMsg->GetSource(), kMsgReply,
            strReply.c_str(), WideBytes(strReply.c_str())));

        return msgHANDLED;
    }

    // ---- CLIENT: check the answer ---------------------------------------
    msgRESULT On_StoreReply(P2PeerMsg* pMsg)
    {
        const std::wstring strBody = PayloadText(pMsg);

        std::wstring aPart[4];
        SplitPipe(strBody, aPart, 4);

        // Match the answer to the query that asked for it, by path.
        const QuerySpec* pSpec = nullptr;
        for (int i = 0; i < kQueryCount; i++)
            if (aPart[0] == kQueries[i].lpszPath) { pSpec = &kQueries[i]; break; }

        CHECK(pSpec != nullptr);
        if (pSpec)
        {
            const bool bExpectMiss = (pSpec->lpszType[0] == L'\0');
            if (bExpectMiss)
            {
                CHECK(aPart[1] == L"MISS");
                wprintf(L"[CLIENT] %-22s -> MISS (as expected)\n", aPart[0].c_str());
            }
            else
            {
                CHECK(aPart[1] == pSpec->lpszType);
                CHECK(aPart[2] == pSpec->lpszValue);
                CHECK(!aPart[3].empty() && aPart[3] != L"0");   // a real P2Pos
                wprintf(L"[CLIENT] %-22s -> %-7s %-12s @pos %s\n",
                        aPart[0].c_str(), aPart[1].c_str(),
                        aPart[2].c_str(), aPart[3].c_str());
            }
        }
        fflush(stdout);

        if (InterlockedIncrement(&g_nAnswered) >= kQueryCount && g_hDoneEvent)
            SetEvent(g_hDoneEvent);

        return msgHANDLED;
    }

    // ---- The dialling side speaks first ---------------------------------
    virtual conRESULT On_ConLoginAck(P2PeerCon*   pCon,
                                     P2PaddrSTR   strThisP2Paddr,
                                     P2PaddrSTR   strThatP2Paddr,
                                     const void*  pvLoginAck,
                                     P2Psize_t    iSize) override
    {
        conRESULT result = P2PeerHub::On_ConLoginAck(
                               pCon, strThisP2Paddr, strThatP2Paddr, pvLoginAck, iSize);

        if (!m_bServer && !m_bSent)
        {
            LogAt(L"CLIENT", L"login acked - issuing the queries");
            for (int i = 0; i < kQueryCount; i++)
                PostP2PeerMsg(new P2PeerMsg32(
                    kClientAddr, kServerAddr, kMsgQuery,
                    kQueries[i].lpszPath, WideBytes(kQueries[i].lpszPath)));
            m_bSent = true;
        }
        return result;
    }

private:
    // The catalogue the server serves. Built once, in the constructor, on the
    // MAIN thread -- before SpawnHub() exists to race with it.
    void BuildCatalogue()
    {
        m_pStore = new P2PmsgMgr(VBLock_Addr64, 4096, 1u << 20);
        m_pStore->r_Desc(P3PmsgField::AttrCMD_Create);

        m_pStore->DeclareItem(L"Product", P3PmsgData(L"Chartboard"));

        P3PmsgField& oRelease = m_pStore->DeclareItem(L"Release", P3PmsgData(L""));
        oRelease.DeclareItem(L"Major", P3PmsgData((int)7));
        oRelease.DeclareItem(L"Minor", P3PmsgData((int)12));

        P3PmsgField& oLimits = m_pStore->DeclareItem(L"Limits", P3PmsgData(L""));
        oLimits.DeclareItem(L"MaxSeries", P3PmsgData((int)512));
        oLimits.DeclareItem(L"MaxPoints", P3PmsgData((int)1000000));
    }

private:
    bool        m_bServer;
    bool        m_bSent;
    P2PmsgMgr*  m_pStore;
};

// -------------------------------------------------------------------------
// The map. One class, two roles: a message is routed to the hub its
// DESTINATION address names, so the server only ever sees StoreQuery and the
// client only ever sees StoreReply. The unused handler on each end simply
// never fires. Framework messages (BCast, Error, ...) fall through to the
// base P2PeerHub map beneath these entries.
// -------------------------------------------------------------------------
BEGIN_P2PeerMsg_MAP(QueryHub, P2PeerHub)
    ON_P2PeerMsg(kMsgQuery, On_StoreQuery)   // server side
    ON_P2PeerMsg(kMsgReply, On_StoreReply)   // client side
END_P2PeerMsg_MAP()


// =========================================================================
// main
// =========================================================================
int main(int /*argc*/, char* /*argv*/[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== WsaQueryTest - querying a Msgcore store across two hubs ===\n");
    wprintf(L"Port : %d (127.0.0.1), %d queries\n\n", (int)kTestPort, kQueryCount);
    fflush(stdout);

    g_hDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!StartupP2Pmsg(16))
    {
        wprintf(L"FATAL: StartupP2Pmsg() failed.\n");
        return 1;
    }
    WSADATA oWsaData;
    WSAStartup(MAKEWORD(2, 2), &oWsaData);

    // ---- Hub A: SERVER (owns the store) ----------------------------------
    QueryHub oServer(kServerAddr, /*bServer*/ true);
    //  ARMING: RequireAuth defaults to ON since ProductionPlan.md Stage 3
    //  step 8, and a hub that requires authentication it cannot enforce
    //  REFUSES TO ARM - SpawnHub() returns NULL rather than starting and
    //  then turning every peer away. This example provisions no identity
    //  and no allow-list, so it takes the documented one-line migration
    //  and says so out loud. NOT the posture to copy into a real hub.
    oServer.RequireAuth ( false );
    HANDLE hServerThread = oServer.SpawnHub();
    if (!hServerThread) { wprintf(L"FATAL: server SpawnHub failed.\n"); return 1; }
    LogAt(L"SERVER", L"hub thread started, catalogue loaded");

    P2PeerConWsa* pSvcCon = P2PeerConWsa::ServiceFactory(kClientAddr, kTestPort);
    if (!pSvcCon) { wprintf(L"FATAL: ServiceFactory failed.\n"); return 1; }
    oServer.PostP2PeerCon(pSvcCon);
    LogAt(L"SERVER", L"listening");

    Sleep(750);

    // ---- Hub B: CLIENT ---------------------------------------------------
    QueryHub oClient(kClientAddr, /*bServer*/ false);
    oClient.RequireAuth ( false );          // as above - unprovisioned example
    HANDLE hClientThread = oClient.SpawnHub();
    if (!hClientThread) { wprintf(L"FATAL: client SpawnHub failed.\n"); return 1; }
    LogAt(L"CLIENT", L"hub thread started");

    P2PeerConWsa* pCliCon = P2PeerConWsa::ClientFactory(kServerAddr, L"127.0.0.1", kTestPort);
    if (!pCliCon) { wprintf(L"FATAL: ClientFactory failed.\n"); return 1; }
    oClient.PostP2PeerCon(pCliCon);
    LogAt(L"CLIENT", L"dialling 127.0.0.1");

    // ---- Wait ------------------------------------------------------------
    LogAt(L"MAIN", L"waiting up to 15s for all replies...");
    DWORD dwResult = WaitForSingleObject(g_hDoneEvent, 15000);

    int nExit;
    if (dwResult != WAIT_OBJECT_0)
    {
        wprintf(L"[MAIN] TIMEOUT - %ld of %d replies arrived\n",
                (long)g_nAnswered, kQueryCount);
        nExit = 3;
    }
    else if (g_nFailed != 0)
    {
        LogAt(L"MAIN", L"ALL REPLIES ARRIVED but an answer did not match");
        nExit = 3;
    }
    else
    {
        LogAt(L"MAIN", L"SUCCESS - every query answered correctly from the store");
        nExit = 0;
    }

    // ---- Shutdown --------------------------------------------------------
    LogAt(L"MAIN", L"shutdown begin");
    oClient.CloseHub();
    oServer.CloseHub();
    WaitForSingleObject(hClientThread, 3000);
    WaitForSingleObject(hServerThread, 3000);
    CloseHandle(hClientThread);
    CloseHandle(hServerThread);

    CleanupP2Pmsg();
    if (g_hDoneEvent) { CloseHandle(g_hDoneEvent); g_hDoneEvent = NULL; }
    WSACleanup();

    wprintf(L"\n%ld checks, %ld failed, %ld/%d answered. Done (exit=%d).\n",
            (long)g_nChecks, (long)g_nFailed, (long)g_nAnswered, kQueryCount, nExit);
    fflush(stdout);
    return nExit;
}
