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
// WsaStoreTest.cpp
//
// Msgcore AND Targetcore together: a whole P2PmsgMgr STORE shipped between
// two hubs over a loopback TCP socket, in one process, and rebuilt on the
// far side.
//
// The four pure-Msgcore harnesses in this tree build trees that never leave
// the process. This one answers the obvious next question -- how does a
// Msgcore tree become a message? -- and the answer is the boundary between
// the two libraries:
//
//     Msgcore     owns the tree and can render it as a flat heap IMAGE
//     Targetcore  moves an opaque byte range from one hub to another
//
// Targetcore has no idea what is in the payload. It carries bytes. Making
// those bytes a Msgcore store is entirely the application's business, and
// this harness is that application.
//
// HOW THE IMAGE IS OBTAINED. P2PmsgMgr's heap is already a single contiguous,
// relocation-safe image -- that is the whole point of the VBLock design, and
// it is why Save()/Load() can be a plain block write. But the in-memory
// accessors for it (P2PmsgHeap_pImage / P2PmsgHeap_CreateIOMAGE, MsgVBHeap.h)
// carry no Msgcore_EXT, so they are NOT exported from the DLL and a client
// cannot reach them. The exported route to the same bytes is Save() to a
// file and read it back. That is what this example does, and the extra file
// round-trip is a limitation of the export surface, not of the format.
//
// THE MESH is exactly WsaMeshTest's: hub A listens on 127.0.0.1, hub B
// dials it, each on its own SpawnHub() pump thread, and the side that
// dialled speaks first once its login is acked.
//
// SIZE. One P2PeerMsg is capped at MAX_P2Psize = 32768 bytes
// (P2PeerMsg.h:36), so the store below is built in a small heap to keep the
// image comfortably inside a single frame. A real store would need chunking.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS : the server rebuilt the store and every field matched.
//   1 = SETUP   : startup/factory failure.
//   2 = ASSERT  : an MFC/CRT assertion fired.
//   3 = TIMEOUT : no delivery in time, or a rebuilt field did not match.

#include "stdafx.h"
#include "WsaStoreTest.h"

#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>

#include "P2Pwin32.h"
#include "P2PeerHub.h"
#include "P2PeerConWsa.h"
#include "P2PeerMsg.h"

#include "P2Pmsg.h"
#include "MsgVect.h"
#include "MsgDesc.h"
#include "P2PmsgMgr.h"
#include "Msgexception.h"

#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CWinApp theApp;

// -------------------------------------------------------------------------
// Mesh configuration
// -------------------------------------------------------------------------
static const short      kTestPort   = 7811;
static const P2PaddrSTR kServerAddr = L"MsgStore.Server";
static const P2PaddrSTR kClientAddr = L"MsgStore.Client";

static HANDLE g_hDoneEvent = NULL;

// Checks run on the SERVER hub thread; main reads them after the event, which
// is the barrier that makes that safe.
static volatile LONG g_nChecks = 0;
static volatile LONG g_nFailed = 0;

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


// =========================================================================
// Store <-> bytes
// =========================================================================
//
// Two halves of one seam. Each side owns its OWN scratch file, so nothing is
// shared behind the socket's back -- the bytes really do travel through the
// mesh.
//
static void MakeTempPath(wchar_t* szOut, LPCWSTR lpszLeaf)
{
    wchar_t szDir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, szDir);
    swprintf_s(szOut, MAX_PATH, L"%s%s", szDir, lpszLeaf);
}

// Build the store the client will send.
static void BuildStore(P2PmsgMgr& oMgr)
{
    oMgr.r_Desc(P3PmsgField::AttrCMD_Create);

    oMgr.DeclareItem(L"Sensor",   P3PmsgData(L"thermo-07"));
    oMgr.DeclareItem(L"Interval", P3PmsgData((int)250));
    oMgr.DeclareItem(L"Scale",    P3PmsgData((double)0.125));

    P3PmsgField& oLimits = oMgr.DeclareItem(L"Limits", P3PmsgData(L""));
    oLimits.DeclareItem(L"Low",  P3PmsgData((int)-40));
    oLimits.DeclareItem(L"High", P3PmsgData((int)125));

    P3PmsgVect oSamples(5, L"Samples", P3PmsgData((int)0));
    for (int i = 0; i < 5; i++)
        oSamples.r_data(i).c_int(200 + i);
    oMgr.r_Desc() += oSamples;
}

// Serialise: Save() to a scratch file, then read the file back as bytes.
static bool StoreToBytes(P2PmsgMgr& oMgr, LPCWSTR lpszScratch, std::vector<BYTE>& vOut)
{
    vOut.clear();
    if (!oMgr.Save(lpszScratch, /*bDefragment*/ true))
        return false;

    // Save() holds the file open on the manager, but only against other
    // WRITERS -- the default share mode is FILE_SHARE_READ, so this read of
    // our own live store is legitimate (P2PmsgMgr.h:302-307).
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, lpszScratch, L"rb") != 0 || !fp)
        return false;

    fseek(fp, 0, SEEK_END);
    long nSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (nSize <= 0) { fclose(fp); return false; }

    vOut.resize((size_t)nSize);
    size_t nRead = fread(vOut.data(), 1, (size_t)nSize, fp);
    fclose(fp);
    return nRead == (size_t)nSize;
}

// Deserialise: lay the received bytes down as a file, then Load() it.
static bool BytesToStore(const void* pvData, size_t nSize,
                         LPCWSTR lpszScratch, P2PmsgMgr& oMgr)
{
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, lpszScratch, L"wb") != 0 || !fp)
        return false;
    size_t nWrote = fwrite(pvData, 1, nSize, fp);
    fclose(fp);
    if (nWrote != nSize) return false;

    // Load() validates the image before trusting it -- a truncated or
    // corrupted payload fails here rather than corrupting the heap.
    return oMgr.Load(lpszScratch) != FALSE;
}


// =========================================================================
// StoreHub : one class, both roles
// =========================================================================
class StoreHub : public P2PeerHub
{
public:
    StoreHub(P2PaddrSTR strAddr, bool bServer)
        : P2PeerHub(strAddr)
        , m_bServer(bServer)
        , m_bSent(false)
    {}
    virtual ~StoreHub() {}

protected:
    // ---- receive side ---------------------------------------------------
    virtual msgRESULT On_P2PeerBCast(P2PeerMsg* pMsg) override
    {
        if (m_bServer) RebuildStore(pMsg);
        return msgHANDLED;
    }

    // ---- send side ------------------------------------------------------
    // The dialling side must speak first: the listening hub's own login leg
    // completes at a different instant, so a message posted before the ack
    // has nowhere to go yet.
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
            LogAt(L"CLIENT", L"login acked - serialising the store");
            PostStore();
            m_bSent = true;
        }
        return result;
    }

private:
    void PostStore()
    {
        wchar_t szScratch[MAX_PATH];
        MakeTempPath(szScratch, L"mscs_wsastore_send.p2p");
        _wremove(szScratch);

        std::vector<BYTE> vImage;
        {
            // A 2 KB heap keeps the image inside one 32 KB P2PeerMsg frame.
            P2PmsgMgr oMgr(VBLock_Addr64, 2048, 1u << 20);
            BuildStore(oMgr);

            if (!StoreToBytes(oMgr, szScratch, vImage))
            {
                LogAt(L"CLIENT", L"FATAL: could not serialise the store");
                return;
            }
        }
        _wremove(szScratch);

        if (vImage.size() >= (size_t)MAX_P2Psize)
        {
            wprintf(L"[CLIENT] store image %zu bytes exceeds MAX_P2Psize %d\n",
                    vImage.size(), (int)MAX_P2Psize);
            fflush(stdout);
            return;
        }

        // The payload is opaque to Targetcore -- a byte range and a length.
        P2PeerMsg32* pMsg = new P2PeerMsg32(
            kClientAddr, kServerAddr, P2Pmsg_BCast,
            vImage.data(), (P2Psize_t)vImage.size());

        PostP2PeerMsg(pMsg);
        wprintf(L"[CLIENT] posted a %zu-byte Msgcore store image over loopback TCP\n",
                vImage.size());
        fflush(stdout);
    }

    void RebuildStore(P2PeerMsg* pMsg)
    {
        const void* pvData = pMsg ? pMsg->Data() : nullptr;
        size_t      nSize  = pMsg ? (size_t)pMsg->DataSize() : 0;

        wprintf(L"\n[SERVER] received %d bytes from '%s'\n",
                (int)nSize, pMsg ? pMsg->GetSource() : L"<null>");
        fflush(stdout);

        CHECK(pvData != nullptr);
        CHECK(nSize > 0);
        if (!pvData || nSize == 0) { SetEvent(g_hDoneEvent); return; }

        wchar_t szScratch[MAX_PATH];
        MakeTempPath(szScratch, L"mscs_wsastore_recv.p2p");
        _wremove(szScratch);

        try
        {
            P2PmsgMgr oMgr;
            CHECK(BytesToStore(pvData, (size_t)nSize, szScratch, oMgr));
            CHECK(oMgr.IsValid());

            // Everything the client put in must come back out, with types.
            CHECK(oMgr.Exists(L"Sensor"));
            CHECK(wcscmp(oMgr.SelectItem(L"Sensor").c_wstr(), L"thermo-07") == 0);
            CHECK(oMgr.SelectItem(L"Interval").c_int() == 250);
            CHECK(oMgr.SelectItem(L"Scale").c_double() == 0.125);

            CHECK(oMgr.SelectItem(L"Limits").SelectItem(L"Low").c_int()  == -40);
            CHECK(oMgr.SelectItem(L"Limits").SelectItem(L"High").c_int() == 125);

            P3PmsgVect oSamples(oMgr.r_Desc().SelectVect(L"Samples").r_Object());
            CHECK((int)oSamples.GetCount() == 5);
            CHECK(oSamples.r_data(0).c_int() == 200);
            CHECK(oSamples.r_data(4).c_int() == 204);

            wprintf(L"[SERVER] rebuilt store: Sensor='%s' Interval=%d Scale=%g "
                    L"Limits=[%d,%d] Samples=%d\n",
                    oMgr.SelectItem(L"Sensor").c_wstr(),
                    oMgr.SelectItem(L"Interval").c_int(),
                    oMgr.SelectItem(L"Scale").c_double(),
                    oMgr.SelectItem(L"Limits").SelectItem(L"Low").c_int(),
                    oMgr.SelectItem(L"Limits").SelectItem(L"High").c_int(),
                    (int)oSamples.GetCount());
            fflush(stdout);
        }
        catch (P2Pevent* pEVT)
        {
            InterlockedIncrement(&g_nFailed);
            const CString strMessage = pEVT ? pEVT->GetMessage() : CString(L"<null>");
            wprintf(L"[SERVER] P2Pevent while rebuilding: %s\n", (LPCWSTR)strMessage);
            fflush(stdout);
            // Isolate/Cancel rather than let it escape: an exception leaving a
            // hub handler is caught by the pump, which then DROPS the
            // connection (P2Pwin32.cpp:3279).
            if (pEVT) pEVT->Cancel(false);
        }

        _wremove(szScratch);
        if (g_hDoneEvent) SetEvent(g_hDoneEvent);
    }

private:
    bool m_bServer;
    bool m_bSent;
};


// =========================================================================
// main
// =========================================================================
int main(int /*argc*/, char* /*argv*/[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(AssertReportHook);

    wprintf(L"=== WsaStoreTest - a Msgcore store shipped between two hubs ===\n");
    wprintf(L"Port : %d (127.0.0.1)\n\n", (int)kTestPort);
    fflush(stdout);

    g_hDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!StartupP2Pmsg(16))
    {
        wprintf(L"FATAL: StartupP2Pmsg() failed.\n");
        return 1;
    }
    // StartupP2Pmsg() does NOT init Winsock -- only P2PeerService::Run() does,
    // and a directly-driven hub never goes through it.
    WSADATA oWsaData;
    WSAStartup(MAKEWORD(2, 2), &oWsaData);

    // ---- Hub A: SERVER ---------------------------------------------------
    StoreHub oServer(kServerAddr, /*bServer*/ true);
    //  ARMING: RequireAuth defaults to ON since ProductionPlan.md Stage 3
    //  step 8, and a hub that requires authentication it cannot enforce
    //  REFUSES TO ARM - SpawnHub() returns NULL rather than starting and
    //  then turning every peer away. This example provisions no identity
    //  and no allow-list, so it takes the documented one-line migration
    //  and says so out loud. NOT the posture to copy into a real hub.
    oServer.RequireAuth ( false );
    HANDLE hServerThread = oServer.SpawnHub();
    if (!hServerThread) { wprintf(L"FATAL: server SpawnHub failed.\n"); return 1; }
    LogAt(L"SERVER", L"hub thread started");

    // ServiceFactory's first arg is the EXPECTED remote peer address.
    P2PeerConWsa* pSvcCon = P2PeerConWsa::ServiceFactory(kClientAddr, kTestPort);
    if (!pSvcCon) { wprintf(L"FATAL: ServiceFactory failed.\n"); return 1; }
    oServer.PostP2PeerCon(pSvcCon);
    LogAt(L"SERVER", L"listening");

    // Let the server pump reach Listen()/Accept() before the client dials.
    Sleep(750);

    // ---- Hub B: CLIENT ---------------------------------------------------
    StoreHub oClient(kClientAddr, /*bServer*/ false);
    oClient.RequireAuth ( false );          // as above - unprovisioned example
    HANDLE hClientThread = oClient.SpawnHub();
    if (!hClientThread) { wprintf(L"FATAL: client SpawnHub failed.\n"); return 1; }
    LogAt(L"CLIENT", L"hub thread started");

    P2PeerConWsa* pCliCon = P2PeerConWsa::ClientFactory(kServerAddr, L"127.0.0.1", kTestPort);
    if (!pCliCon) { wprintf(L"FATAL: ClientFactory failed.\n"); return 1; }
    oClient.PostP2PeerCon(pCliCon);
    LogAt(L"CLIENT", L"dialling 127.0.0.1");

    // ---- Wait ------------------------------------------------------------
    LogAt(L"MAIN", L"waiting up to 15s for the store to arrive...");
    DWORD dwResult = WaitForSingleObject(g_hDoneEvent, 15000);

    int nExit;
    if (dwResult != WAIT_OBJECT_0)
    {
        LogAt(L"MAIN", L"TIMEOUT - nothing delivered");
        nExit = 3;
    }
    else if (g_nFailed != 0)
    {
        LogAt(L"MAIN", L"DELIVERED but the rebuilt store did not match");
        nExit = 3;
    }
    else
    {
        LogAt(L"MAIN", L"SUCCESS - store rebuilt intact on the far hub");
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

    wprintf(L"\n%ld checks, %ld failed. Done (exit=%d).\n",
            (long)g_nChecks, (long)g_nFailed, nExit);
    fflush(stdout);
    return nExit;
}
