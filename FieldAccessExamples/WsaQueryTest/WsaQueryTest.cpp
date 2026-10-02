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
// FIELDACCESSEXAMPLES PORT of DirectExamples\WsaQueryTest. Same subject, same
// mesh, same message map, same queries; what changed is how named values are
// written and read (Msgcore/MsgFieldRef.hpp):
//
//   * the catalogue is built through TYPED VIEWS -- `cat->Product = ...`,
//     `release->Major = 7` -- and checked against the long-hand calls;
//   * the query and the reply are no longer a NUL-terminated string and a
//     "path|TYPE|value|p2pos" string to split: each is a set of named APP
//     FIELDS on the P2PeerMsg itself (Targetcore/P2PeerAppFields.hpp), written
//     and read through a typed view over AppFields(*pMsg) --
//     `reply->type = ...`, `std::wstring t = reply->type`. The P2Pos travels
//     as a 64-bit integer rather than as digits.
//
// What stayed plain: the mesh, hub and connection code; RootPath2Object, which
// resolves a DOTTED run-time path (Field() takes one name per segment); and
// ToStringType()/ToString(), which render a value whose type the server does
// not know in advance. Extra checks show the reply's field index and coding.
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
// is behind it, not the encoding. Both messages carry an EMPTY payload and
// named app fields (P2PeerAppFields.hpp, the facade's own field format):
//
//   StoreQuery   path                       (text)
//   StoreReply   path, type, value          (text)
//                pos                        (64-bit int; 0 when there is none)
//
// The DirectExamples original flattened the same four values into one
// delimited string; named fields are what that string was standing in for.
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
#include "MsgFieldRef.hpp"
#include "P2PeerAppFields.hpp"

#include <string>
#include <vector>

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
// The two messages' fields, as typed views over AppFields(*pMsg).
//
// AppFields() anchors every name under the message's "P2PF$Fields" item, never
// the root, so a view cannot reach the routing envelope (Src/Dst/...). Values
// are stored in the Bytes coding, which a TargetFacade client reads unchanged.
// -------------------------------------------------------------------------
struct QueryFields : MsgView
{
    MSG_FIELD ( path, std::wstring );
};

struct ReplyFields : MsgView
{
    MSG_FIELD ( path,  std::wstring );
    MSG_FIELD ( type,  std::wstring );     // Msgcore type tag, or MISS / ERR
    MSG_FIELD ( value, std::wstring );     // rendered value
    MSG_FIELD ( pos,   long long );        // the P2Pos row handle, 0 if none
};

// Both messages carry their content in fields, so the payload is empty. A
// non-NULL source still selects P3PmsgData's blob constructor, as
// FacadeHub::PostMsg does with its own kEmptyPayload.
static const BYTE kEmptyPayload = 0;

static P2PeerMsg32* NewFieldMsg(P2PaddrSTR strSrc, P2PaddrSTR strDst, P2PmsgID strName)
{
    return new P2PeerMsg32(strSrc, strDst, strName, &kEmptyPayload, 0);
}

// -------------------------------------------------------------------------
// The catalogue's schema, as typed views.
// -------------------------------------------------------------------------
struct CatalogueView : MsgView
{
    MSG_FIELD ( Product, std::wstring );
};
struct ReleaseView : MsgView
{
    MSG_FIELD ( Major, int );
    MSG_FIELD ( Minor, int );
};
struct LimitsView : MsgView
{
    MSG_FIELD ( MaxSeries, int );
    MSG_FIELD ( MaxPoints, int );
};

// A view below the root is bound with MsgFieldAnchor::Child(store, L"name"),
// which finds the child afresh by name on every access. Binding MsgViewOf to
// m_pStore->SelectItem(...) instead would hold the store's CURSOR item, which
// the next lookup on the store retargets.


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
        // The query's one field. A message without it is answered, not
        // thrown out of the handler (which would drop the link -- see below).
        MsgViewOf<QueryFields> query(AppFields(*pMsg));
        const bool bHasPath = query->path.Exists();
        CHECK(bHasPath);                                   // ADDED
        const std::wstring strPath = bHasPath ? query->path.Get() : std::wstring();

        std::wstring strType, strValue;
        long long    llPos = 0;

        if (!m_pStore)
        {
            strType  = L"ERR";
            strValue = L"no store";
        }
        else
        {
            try
            {
                // Plain: RootPath2Object resolves a DOTTED run-time path in
                // one call; Field() takes one name per segment.
                P3PmsgField oField(m_pStore->RootPath2Object(strPath.c_str()));

                // The answer is self-describing: the type tag travels with the
                // value, so the caller never has to guess how to read it, and
                // the P2Pos gives it a stable handle to ask again later.
                // Plain: ToStringType()/ToString() render a value of ANY tag;
                // a field read needs its type known in advance.
                strType  = oField.ToStringType();
                strValue = oField.ToString();
                llPos    = (long long)oField.GetP2Pos();
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
                strType  = L"MISS";
                strValue.clear();
                llPos    = 0;
                if (pEVT) pEVT->Cancel(false);
            }
        }

        wprintf(L"[SERVER] '%s' -> %s '%s' @pos %lld\n",
                strPath.c_str(), strType.c_str(), strValue.c_str(), llPos);
        fflush(stdout);

        // Reply source = this hub; destination = whoever asked. The fields go
        // on BEFORE the post: after it, the message is not ours.
        P2PeerMsg32* pReply = NewFieldMsg(kServerAddr, pMsg->GetSource(), kMsgReply);
        {
            MsgViewOf<ReplyFields> reply(AppFields(*pReply));
            reply->path  = strPath;
            reply->type  = strType;
            reply->value = strValue;
            reply->pos   = llPos;
        }
        PostP2PeerMsg(pReply);

        return msgHANDLED;
    }

    // ---- CLIENT: check the answer ---------------------------------------
    msgRESULT On_StoreReply(P2PeerMsg* pMsg)
    {
        try
        {
            MsgViewOf<ReplyFields> reply(AppFields(*pMsg));
            const std::wstring strPath  = reply->path;
            const std::wstring strType  = reply->type;
            const std::wstring strValue = reply->value;
            const long long    llPos    = reply->pos;

            // Match the answer to the query that asked for it, by path.
            const QuerySpec* pSpec = nullptr;
            for (int i = 0; i < kQueryCount; i++)
                if (strPath == kQueries[i].lpszPath) { pSpec = &kQueries[i]; break; }

            CHECK(pSpec != nullptr);
            if (pSpec)
            {
                const bool bExpectMiss = (pSpec->lpszType[0] == L'\0');
                if (bExpectMiss)
                {
                    CHECK(strType == L"MISS");
                    wprintf(L"[CLIENT] %-22s -> MISS (as expected)\n", strPath.c_str());
                }
                else
                {
                    CHECK(strType  == pSpec->lpszType);
                    CHECK(strValue == pSpec->lpszValue);
                    CHECK(llPos != 0);                          // a real P2Pos
                    wprintf(L"[CLIENT] %-22s -> %-7s %-12s @pos %lld\n",
                            strPath.c_str(), strType.c_str(),
                            strValue.c_str(), llPos);
                }
            }

            // ADDED: the reply's field index lists its names in the order the
            // server wrote them -- what a facade client's GetFieldName walks.
            const std::vector<std::wstring> aNames = AppFieldNames(*pMsg);
            CHECK(aNames.size() == 4 && aNames[0] == L"path" && aNames[1] == L"type" &&
                  aNames[2] == L"value" && aNames[3] == L"pos");

            // ADDED: AppFields stores in the Bytes coding -- an 8-byte blob,
            // not an INT64 tag -- and the dynamic form reads it the same.
            CHECK(AppField(*pMsg, L"pos").DataType() == VBLockData_BLOB16 &&
                  AppField(*pMsg, L"pos").AsInt64()  == llPos);
        }
        catch (P2Pevent* pEVT)
        {
            // Never let it escape the handler: the pump would drop the link.
            InterlockedIncrement(&g_nFailed);
            const CString strMsg = pEVT ? pEVT->GetMessage() : CString(L"<null>");
            wprintf(L"[CLIENT] P2Pevent reading a reply: %s\n", (LPCWSTR)strMsg);
            if (pEVT) pEVT->Cancel(false);
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
            {
                P2PeerMsg32* pQuery = NewFieldMsg(kClientAddr, kServerAddr, kMsgQuery);
                {
                    MsgViewOf<QueryFields> query(AppFields(*pQuery));
                    query->path = kQueries[i].lpszPath;
                }
                PostP2PeerMsg(pQuery);
            }
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

        // P2PmsgMgr derives from P3PmsgField and holds still, so it anchors
        // a view directly; Release and Limits are its named children.
        MsgViewOf<CatalogueView> cat(*m_pStore);
        cat->Product = L"Chartboard";

        MsgViewOf<ReleaseView> release(MsgFieldAnchor::Child(*m_pStore, L"Release"));
        release->Major = 7;                 // the first write creates "Release"
        release->Minor = 12;

        MsgViewOf<LimitsView> limits(MsgFieldAnchor::Child(*m_pStore, L"Limits"));
        limits->MaxSeries = 512;
        limits->MaxPoints = 1000000;

        // ADDED: what the views wrote is what the long-hand calls and the
        // dynamic form read -- and what RootPath2Object will find.
        CHECK(wcscmp(m_pStore->SelectItem(L"Product").c_wstr(), L"Chartboard") == 0);
        CHECK(m_pStore->SelectItem(L"Release").SelectItem(L"Minor").c_int() == release->Minor.Get());
        CHECK(Field(*m_pStore, L"Limits")[L"MaxPoints"].AsInt() == 1000000);
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
