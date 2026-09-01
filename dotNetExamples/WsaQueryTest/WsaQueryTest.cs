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
// WsaQueryTest.cs
//
// The other half of the pair with WsaStoreTest: there the whole store MOVES,
// here it STAYS PUT and is queried across the mesh a field at a time. The
// counterpart of ..\ComExamples\WsaQueryTestCom.
//
// Which is the shape most systems actually want. A store is a document and a
// document does not fit in a message -- MaxPayload is 24 KB and a real one is
// megabytes -- so the useful pattern is a lookup service: one hub owns the
// store, the other asks it questions.
//
// THREE THINGS THIS HARNESS EXISTS TO PIN:
//
//  1. THE ANSWER TO A FAILED LOOKUP MUST BE AN ANSWER. The original C++
//     finding was that RootPath2Object reports a missing path by THROWING, and
//     that an exception escaping a hub handler makes the pump drop the
//     connection -- so the handler had to convert it into a reply. At this tier
//     the throw is long gone: MsgcoreCom catches at every entry point and
//     FieldAt answers msgcNoField, which the CLR then raises as a COMException
//     the handler catches with Api.Call. Section 3 checks that the link is
//     provably still up afterwards.
//
//  2. THE CORRELATION TAG. The kernel carries a per-message tag beside the
//     payload, so a reply can be matched to its request without either of them
//     spending a byte of payload on saying which. SendEx and MsgTag are how
//     this tier reaches it. Section 2.
//
//  3. NO BEGIN_P2PeerMsg_MAP, AND THEREFORE NO LNK1194. The C++ harness cannot
//     delay-load TargetCore.dll, because BEGIN_P2PeerMsg_MAP imports the DATA
//     symbol P2PeerHub::P2PeerMsgMap and the linker refuses /DELAYLOAD on a DLL
//     an image imports data from. This assembly links neither DLL -- it links
//     nothing but mscorlib, System and System.Core -- so the constraint does
//     not arise at all. It is the clearest single benefit of this tier, and it
//     is stronger here than in the C++ COM tree, which still links ole32.
//
// Exit codes: 0 SUCCESS  1 SETUP  3 a check failed / timed out.

using System;
using System.Collections.Generic;

using MsgcoreCom;
using Msgc;
using static Msgc.Harness;

static class WsaQueryTest
{
    const int    Port        = 7842;
    const string ServerAddr  = "MsgQuery.Server";
    const string ClientAddr  = "MsgQuery.Client";
    const string TopicAsk    = "ask";
    const string TopicAnswer = "answer";

    // The wire form, kept deliberately trivial: a request is a path, a reply is
    // "OK<tab>value" or "ERR<tab>0x........". Msgcore itself is not used to
    // encode the message -- that is what WsaStoreTest does -- because the point
    // here is the LOOKUP, and a hand-rolled two-field reply keeps it in view.
    static string MakeOk  (string v)  { return "OK\t" + v; }
    static string MakeErr (int hr)    { return string.Format("ERR\t0x{0:X8}", hr); }

    sealed class Reply { public int Tag; public string Body; }


    [STAThread]
    static int Main ()
    {
        InitConsole();
        Console.WriteLine("=== WsaQueryTestNet - querying a Msgcore store across two hubs ===");
        Console.WriteLine("Port : {0} (127.0.0.1)   apartment: STA   servers: MsgcoreCom + TargetCom\n", Port);

        // ---- the store, which never leaves this hub -------------------------
        Section("1. One hub owns a store; the other will only ever ask about it");

        int hr;
        var store = Store.Create(out hr);
        if (store == null) return SetupFailure("new MsgcoreCom.MsgStore", hr);

        var cfg = store.Root.Declare("config", 0, true);
        cfg.Declare("width",  1024, true);
        cfg.Declare("height", 768, true);
        cfg.Declare("title",  "Ivyware € Chartboard", true);

        var win = cfg.Declare("window", 0, true);
        win.Declare("x", 100, true);
        win.Declare("y", 200, true);

        Check(Convert.ToInt32(store.Com.FieldAt(".config.window.y").Value) == 200, "the store is built");
        Note("store: {0} bytes, config has {1} children", store.Com.Size, cfg.Count);

        // ---- the mesh ---------------------------------------------------------
        Section("2. Two hubs, loopback TCP, request and reply correlated by tag");

        int hrNet;
        var net = P2P.Network.Create(out hrNet);
        if (net == null) return SetupFailure("new TargetCom.P2PNetwork", hrNet);
        Note("{0}", net.VersionString);

        var errors  = new List<string>();
        object errLock = new object();
        int answered = 0;

        int hrHub;
        var server = net.CreateHub(ServerAddr, out hrHub);
        if (server == null) return SetupFailure("CreateHub(server)", hrHub);

        // THE LOOKUP SERVICE. It runs on the server hub's dispatch thread --
        // and unlike the C++ harness next door, it STAYS there, because a
        // managed sink is agile and nothing marshals it into the main STA. It
        // may still take as long as it likes and may call into the store
        // freely, which is what MsgcoreCom's own event queueing exists to
        // guarantee.
        server.OnTopic(TopicAsk, m =>
        {
            string path = m.Text;

            // THE TAG. Read from the hub DURING the OnMessage event -- it is
            // valid there and answers p2pfNoMessage anywhere else -- so the
            // reply can be matched to this request without the payload carrying
            // an id of its own.
            int tag = server.MsgTag;

            // The lookup. A path that does not resolve is an ANSWER, not a
            // throw that escapes and not silence.
            IMsgFieldCom f;
            int hrLookup = Api.Call(() => store.Com.FieldAt(path), out f);

            string body = (hrLookup == Hr.S_OK && f != null)
                        ? MakeOk(Api.Try(() => f.Text, string.Empty))
                        : MakeErr(hrLookup);

            Log("SERVER", "ask tag={0} '{1}' -> {2}", tag, path,
                hrLookup == Hr.S_OK ? "OK" : Hr.Name(hrLookup));

            // SendEx carries the tag back, so the client does not have to guess.
            server.SendEx(ClientAddr, TopicAnswer, body, tag);
            System.Threading.Interlocked.Increment(ref answered);
        });
        server.OnPeerUp(peer => Log("SERVER", "peer up   : {0}", peer));
        server.OnError(w => { lock (errLock) errors.Add(w); });

        hr = server.Listen(ClientAddr, P2P.Endpoint.TcpListen(Port));
        Check(!Hr.Failed(hr), "the server armed its listener");
        if (Hr.Failed(hr)) return SetupFailure("Listen", hr);

        var client = net.CreateHub(ClientAddr, out hrHub);
        if (client == null) return SetupFailure("CreateHub(client)", hrHub);

        var replies  = new List<Reply>();
        object repLock = new object();
        var gotReplies = new Gate();

        client.OnTopic(TopicAnswer, m =>
        {
            var r = new Reply { Tag = client.MsgTag, Body = m.Text };
            lock (repLock) replies.Add(r);
            Log("CLIENT", "answer tag={0} : {1}", r.Tag, r.Body);
            gotReplies.Bump();
        });

        // The five questions, four answerable and one not -- because "what
        // happens to a bad request" is the part of a lookup service that is
        // worth testing.
        var asks = new[] {
            new { Path = ".config.width",    Expect = "1024",                 Ok = true  },
            new { Path = ".config.height",   Expect = "768",                  Ok = true  },
            new { Path = ".config.title",    Expect = "Ivyware € Chartboard", Ok = true  },
            new { Path = ".config.window.y", Expect = "200",                  Ok = true  },
            new { Path = ".config.depth",    Expect = (string)null,           Ok = false },
        };

        client.OnPeerUp(peer =>
        {
            Log("CLIENT", "peer up   : {0} - asking {1} questions", peer, asks.Length);
            for (int i = 0; i < asks.Length; ++i)
                client.SendEx(ServerAddr, TopicAsk, asks[i].Path, 1000 + i);
        });
        client.OnError(w => { lock (errLock) errors.Add(w); });

        hr = client.Connect(ServerAddr, P2P.Endpoint.TcpDial("127.0.0.1", Port));
        Check(!Hr.Failed(hr), "the client armed its dial");
        if (Hr.Failed(hr)) return SetupFailure("Connect", hr);
        Log("CLIENT", "dialling 127.0.0.1:{0}", Port);

        Log("MAIN", "waiting up to 15s for {0} answers (pumping)...", asks.Length);
        bool all = gotReplies.Wait(asks.Length, 15000);

        if (!all)
        {
            int got;
            lock (repLock) got = replies.Count;
            Log("MAIN", "TIMEOUT - only {0} of {1} answers arrived", got, asks.Length);
            Check(false, "every question was answered within 15s");
            return Verdict();
        }

        Check(answered == asks.Length, "the server answered every question");
        Reply[] snapshot;
        lock (repLock) snapshot = replies.ToArray();
        Check(snapshot.Length == asks.Length, "and the client received every answer");

        // ---- what came back ---------------------------------------------------
        Section("3. The answers, matched by tag");

        for (int i = 0; i < asks.Length; ++i)
        {
            int wantTag = 1000 + i;

            Reply found = null;
            foreach (var r in snapshot) if (r.Tag == wantTag) { found = r; break; }

            Check(found != null, "there is a reply tagged " + wantTag);
            if (found == null) continue;

            // THE TAG SURVIVED THE ROUND TRIP, which is the claim: neither
            // request nor reply spent a byte of payload saying which question it
            // was about.
            Check(found.Tag == wantTag, "and the tag came back unchanged");

            if (asks[i].Ok)
            {
                Check(found.Body == MakeOk(asks[i].Expect), "'" + asks[i].Path + "' answered its value");
            }
            else
            {
                // A MISSING PATH CAME BACK AS AN ANSWER. Against the C++ API
                // this is where RootPath2Object throws, and an exception
                // escaping a hub handler makes the pump drop the connection --
                // so the handler had to catch it INSIDE the handler and turn it
                // into a reply. Here FieldAt answers msgcNoField, Api.Call turns
                // that back into a code, and nothing was ever thrown out of the
                // handler at all.
                Check(found.Body == MakeErr(Hr.msgcNoField),
                      "'" + asks[i].Path + "' answered msgcNoField, as an answer");
                Note("'{0}' -> {1} (an answer, not a dropped connection)", asks[i].Path, found.Body);
            }
        }

        // The link survived the bad request -- which is the real assertion
        // behind finding 1 at the top of this file.
        Check(client.IsPeerUp(ServerAddr), "the client still sees the server");
        Check(server.IsPeerUp(ClientAddr), "and the server still sees the client");
        Note("both peers still up after the failed lookup");

        // MsgTag OUTSIDE a handler answers nothing, which is what makes it safe
        // to read inside one: it is a property of the message being delivered,
        // not of the hub.
        Check(client.MsgTag == 0, "MsgTag outside a handler is not a stale tag");

        // ---- the store is untouched, and still writable -----------------------
        Section("4. The store never moved");

        Check(store.Com.IsValid, "the store is still valid");
        Check(store.Com.FieldAt(".config").Count == 4, "with the same shape");
        Check(Convert.ToInt32(store.Com.FieldAt(".config.width").Value) == 1024, "and the same values");

        // A query service is a READER; the owner can still write. Serving five
        // lookups changed nothing about the store's own shape.
        store.Com.FieldAt(".config.width").Value = 1920;
        Check(Convert.ToInt32(store.Com.FieldAt(".config.width").Value) == 1920, "the owner can still write");
        Check(store.Com.Filename.Length == 0, "and it never touched a file -- it stayed in memory");

        Log("MAIN", "shutdown begin");
        client.Dispose();
        server.Dispose();
        net.Dispose();
        store.Dispose();

        return Verdict();
    }
}
