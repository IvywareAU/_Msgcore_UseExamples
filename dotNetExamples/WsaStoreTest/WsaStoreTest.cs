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
// WsaStoreTest.cs
//
// A whole Msgcore store serialised, sent between two hubs over loopback TCP,
// and rebuilt on the far side. The counterpart of
// ..\ComExamples\WsaStoreTestCom.
//
// ONE OF THE TWO HARNESSES IN THIS TREE THAT USES BOTH SERVERS, and the one
// that shows why they are two:
//
//      MsgcoreCom   what is IN a message  -- the store, its tree, its types
//      TargetCom    moving it             -- hubs, connections, delivery
//
// Neither knows about the other. The store becomes a byte array and the byte
// array becomes a payload, and that is the whole of the join -- which is
// exactly the relationship Msgcore and Targetcore have one tier down, preserved
// rather than papered over.
//
// THE ROUND TRIP THROUGH A FILE IS NOT LAZINESS. A P2PmsgMgr's heap is already
// one contiguous, relocation-safe image -- that is why Save can be a plain
// block write -- but the accessors for it (P2PmsgHeap_pImage, _pIOmage,
// _CreateIOMAGE) carry no Msgcore_EXT, so they are not exported from the DLL
// and Msgcore_c.h cannot publish them either. The only exported route to those
// bytes is Save() to a file and read the file. Exporting those five functions
// would remove the round trip from every tier at once.
//
// Two hubs in ONE PROCESS, each on its own pump thread inside the facade,
// joined by a loopback TCP connection -- the WsaMeshTest mesh.
//
// Exit codes: 0 SUCCESS  1 SETUP  3 a check failed / timed out.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

using MsgcoreCom;
using Msgc;
using static Msgc.Harness;

static class WsaStoreTest
{
    const int    Port       = 7841;
    const string ServerAddr = "MsgStore.Server";
    const string ClientAddr = "MsgStore.Client";
    const string TopicStore = "store";

    static readonly DateTime Unset = new DateTime(1899, 12, 30);

    // -------------------------------------------------------------------
    // The store the client will ship. Deliberately varied: several declared
    // widths, a nested subtree, an attribute and a timestamp, so that what is
    // checked on the far side is the SHAPE and the TYPES and not just one
    // number.
    // -------------------------------------------------------------------
    static void BuildStore (Store store)
    {
        var order = store.Root.Declare("Order", 0, true);
        order.Declare("Customer", "Ivyware Pty Ltd", true);
        order.Declare("Total",    1299.50, true);
        order.Declare("Paid",     false, true);
        order.DeclareTyped("OrderId", 10045, MsgDataType.UInt32, true);
        order.DeclareTyped("Lines",   3,     MsgDataType.UInt08, true);
        order.DeclareTyped("Ref",     90071992547409L, MsgDataType.Int64, true);

        var ship = order.Declare("ShipTo", "", true);
        ship.Declare("City",     "Melbourne", true);
        ship.Declare("Postcode", 3000, true);
        // U+20AC, so the far side proves the UTF-16 payload survived the wire.
        ship.Declare("Note", "leave at door €", true);

        var a = order.Child("Total").Attributes(true);
        a.Declare("Currency", "AUD", true);

        order.Touch();
    }

    // Everything the far side must find. Written once so that "the store
    // arrived" is one call on both sides and cannot drift between them.
    static bool VerifyStore (Store store, DateTime stamp)
    {
        bool ok = true;
        Action<bool, string> want = (b, what) =>
        {
            if (b) return;
            ok = false;
            Console.WriteLine("  MISMATCH: {0}", what);
        };

        want(store.Root.Count == 1, "root has one child");
        want(store.Com.FieldAt(".Order").Count == 7, "Order has seven");
        want((string)store.Com.FieldAt(".Order.Customer").Value == "Ivyware Pty Ltd", "Customer");
        want((double)store.Com.FieldAt(".Order.Total").Value == 1299.50, "Total");
        want((bool)store.Com.FieldAt(".Order.Paid").Value == false, "Paid");
        want(store.Com.FieldAt(".Order.Paid").TypeName == "BOOL", "Paid is BOOL");

        // The DECLARED WIDTHS, which are the part a naive transport loses.
        want(store.Com.FieldAt(".Order.OrderId").TypeName == "UINT32", "OrderId is UINT32");
        want(Convert.ToInt32(store.Com.FieldAt(".Order.OrderId").Value) == 10045, "OrderId value");
        want(store.Com.FieldAt(".Order.Lines").TypeName == "UINT08", "Lines is UINT08");
        want(Convert.ToInt32(store.Com.FieldAt(".Order.Lines").Value) == 3, "Lines value");
        want(store.Com.FieldAt(".Order.Ref").TypeName == "INT64", "Ref is INT64");
        want(Convert.ToInt64(store.Com.FieldAt(".Order.Ref").Value) == 90071992547409L, "Ref value");

        want((string)store.Com.FieldAt(".Order.ShipTo.City").Value == "Melbourne", "City");
        want(Convert.ToInt32(store.Com.FieldAt(".Order.ShipTo.Postcode").Value) == 3000, "Postcode");
        want((string)store.Com.FieldAt(".Order.ShipTo.Note").Value == "leave at door €", "Note (U+20AC)");

        // The parallel tree came too.
        IMsgAttrCom a;
        if (Api.Call(() => store.Com.FieldAt(".Order.Total").Attributes(false), out a) == Hr.S_OK)
            want((string)a.Item("Currency").Value == "AUD", "the @Currency attribute");
        else
            want(false, "the attribute collection survived");

        want(store.Com.FieldAt(".Order").Timestamp == stamp, "the timestamp");

        return ok;
    }


    [STAThread]
    static int Main ()
    {
        InitConsole();
        Console.WriteLine("=== WsaStoreTestNet - a Msgcore store shipped between two hubs ===");
        Console.WriteLine("Port : {0} (127.0.0.1)   apartment: STA   servers: MsgcoreCom + TargetCom\n", Port);

        // ---- tier 1: the content -------------------------------------------
        Section("1. Build the store (MsgcoreCom)");

        int hr;
        var source = Store.Create(out hr);
        if (source == null) return SetupFailure("new MsgcoreCom.MsgStore", hr);

        BuildStore(source);
        DateTime stamp = source.Com.FieldAt(".Order").Timestamp;
        Check(stamp != Unset, "the source store is stamped");
        Check(VerifyStore(source, stamp), "and verifies before it goes anywhere");
        Note("built: {0} bytes of heap, root '{1}'", source.Com.Size, source.Com.RootName);

        // ---- serialise -------------------------------------------------------
        Section("2. Serialise -- Save to a file and read the bytes back");

        string sendFile = Scratch.Path("WsaStoreTestNet.send.p2p");
        string recvFile = Scratch.Path("WsaStoreTestNet.recv.p2p");
        Scratch.Remove(sendFile, recvFile);

        source.Com.Save(sendFile);
        byte[] image = File.ReadAllBytes(sendFile);
        Check(image.Length != 0, "the image has bytes");
        Note("image: {0} bytes (via a file, because the in-memory image is not exported)", image.Length);

        // ---- tier 2: the transport --------------------------------------------
        Section("3. Two hubs, one process, loopback TCP (TargetCom)");

        int hrNet;
        var net = P2P.Network.Create(out hrNet);
        if (net == null) return SetupFailure("new TargetCom.P2PNetwork", hrNet);
        Note("{0}", net.VersionString);

        int maxPayload = net.MaxPayload;
        Check(maxPayload > 0, "the facade publishes a payload cap");
        Note("MaxPayload: {0} bytes -- a bigger store needs chunking", maxPayload);

        // The cap is real and this harness stays under it deliberately, rather
        // than discovering it at Send time.
        Check(image.Length < maxPayload, "and this store fits under it");

        var arrived  = new Gate();
        byte[] received = null;
        string fromWho  = null;
        var errors = new List<string>();
        object errLock = new object();

        // EVERY HANDLER BELOW RUNS ON A DISPATCH THREAD INSIDE THE FACADE, one
        // per hub, concurrently with this one -- a managed sink is agile, so
        // nothing marshals it back into this apartment. That is why the shared
        // state here is written under a lock and read after the gate, and it is
        // the difference from the C++ harness next door, whose sink is
        // apartment-bound and single-threaded by construction.
        int hrHub;
        var server = net.CreateHub(ServerAddr, out hrHub);
        if (server == null) return SetupFailure("CreateHub(server)", hrHub);

        server.OnTopic(TopicStore, m =>
        {
            fromWho  = m.Source;
            received = m.Payload;
            Log("SERVER", "received {0} bytes on topic '{1}' from '{2}'", m.Size, m.Topic, m.Source);
            arrived.Bump();
        });
        server.OnPeerUp(peer => Log("SERVER", "peer up   : {0}", peer));
        server.OnError(what => { lock (errLock) errors.Add(what); Log("SERVER", "error     : {0}", what); });

        hr = server.Listen(ClientAddr, P2P.Endpoint.TcpListen(Port));
        Check(!Hr.Failed(hr), "the server armed its listener");
        if (Hr.Failed(hr)) return SetupFailure("Listen", hr);
        Log("SERVER", "listening for '{0}' on port {1}", ClientAddr, Port);

        var client = net.CreateHub(ClientAddr, out hrHub);
        if (client == null) return SetupFailure("CreateHub(client)", hrHub);

        client.OnPeerUp(peer =>
        {
            Log("CLIENT", "peer up   : {0} - shipping the store", peer);
            int hrSend = client.Send(ServerAddr, TopicStore, image);
            Log("CLIENT", "Send      : {0} ({1} bytes)", Hr.Name(hrSend), image.Length);
        });
        client.OnError(what => { lock (errLock) errors.Add(what); Log("CLIENT", "error     : {0}", what); });

        hr = client.Connect(ServerAddr, P2P.Endpoint.TcpDial("127.0.0.1", Port));
        Check(!Hr.Failed(hr), "the client armed its dial");
        if (Hr.Failed(hr)) return SetupFailure("Connect", hr);
        Log("CLIENT", "dialling 127.0.0.1:{0} (retries until answered)", Port);

        // THIS PUMPS. The pump is not load-bearing for a managed sink -- see
        // above -- but Gate.Wait does it anyway, because depending on the CCW
        // staying agile is how you get a client that hangs on a customer's
        // machine and not on yours.
        Log("MAIN", "waiting up to 15s for connect + delivery (pumping)...");
        bool ok = arrived.Wait(1, 15000);

        if (!ok)
        {
            Log("MAIN", "TIMEOUT - the store never arrived");
            Check(false, "the store arrived within 15s");
            return Verdict();
        }

        Check(fromWho == ClientAddr, "it came from the client");
        Check(received != null && received.Length == image.Length, "the right number of bytes");
        Check(received != null && ByteEqual(received, image), "byte-identical");
        Note("payload arrived byte-identical: {0} bytes", received == null ? 0 : received.Length);

        // ---- tier 1 again: rebuild ---------------------------------------------
        Section("4. Rebuild the store on the far side (MsgcoreCom)");

        File.WriteAllBytes(recvFile, received);

        int hrRebuilt;
        var rebuilt = Store.Create(out hrRebuilt);
        Check(rebuilt != null, "a store to rebuild into");
        rebuilt.Com.Open(recvFile);

        Check(rebuilt.Com.RootName == source.Com.RootName, "the root name came across");
        Check(VerifyStore(rebuilt, stamp), "and the whole tree verifies on the far side");
        Note("rebuilt: {0} children, Order.Total={1:F2} AUD",
             rebuilt.Root.Count, rebuilt.Com.FieldAt(".Order.Total").Value);

        // AND IT IS A REAL STORE, not a read-only snapshot: the far side can
        // write to it, and its own P2Pos values are its own.
        rebuilt.Com.FieldAt(".Order.Paid").Value = true;
        Check((bool)rebuilt.Com.FieldAt(".Order.Paid").Value, "the far side can write to it");
        Check(!(bool)source.Com.FieldAt(".Order.Paid").Value, "and the sender is untouched");
        Check(rebuilt.Com.FieldAt(".Order").P2Pos != 0, "and it has positions of its own");

        // ---- the one report that is NOT a failure --------------------------------
        //
        // Both hubs raised OnError during the handshake, and it is worth reading
        // rather than suppressing. "MsgStore.Server" and "MsgStore.Client" are
        // SIBLINGS in the dotted address tree -- neither is an ancestor of the
        // other -- so the facade warns that nothing can be ROUTED through this
        // edge and a broadcast will not relay beyond it.
        //
        // That is exactly right and exactly harmless here: this harness sends
        // DIRECT traffic over one link, which a sibling edge carries perfectly.
        // The warning matters when a THIRD hub expects to be reached THROUGH one
        // of these two.
        //
        // It arrives as OnError because the corresponding HRESULT,
        // P2PF_S_UNRELATED_LINK, is a SUCCESS code -- and automation discards
        // those. Listen answers plain S_OK here and there is nothing for a
        // .NET caller to catch.
        Section("5. The topology warning -- a success code that had to become an event");

        Pump.For(300);          // let any late OnError land

        int unrelated = 0, real = 0;
        lock (errLock)
        {
            foreach (var e in errors)
            {
                if (e != null && e.IndexOf("neither ancestor nor descendant", StringComparison.Ordinal) >= 0)
                    ++unrelated;
                else { ++real; Note("unexpected: {0}", e); }
            }
        }
        Check(real == 0, "nothing actually went wrong");
        Check(unrelated == 2, "one sibling-link warning per hub, as expected");
        Note("{0} sibling-link warnings, {1} real errors -- direct traffic is unaffected",
             unrelated, real);

        // AND THE BETTER ANSWER, which only exists because the facade grew a
        // READ SIDE. The event is a sentence that arrives once, in the instant
        // of the call; RelationTo is a CODE that can be asked at any time
        // afterwards, from any tier, and needs no event sink at all. A .NET
        // client that cannot see P2PF_S_UNRELATED_LINK on the return value can
        // still learn the same fact -- and this is the tier where that matters
        // most, because `void` discarded it.
        const int RelUnrelated = 0x0800;
        int rel = Msgc.Api.Try(() => client.Raw.RelationTo(ServerAddr));
        Check((rel & RelUnrelated) != 0, "RelationTo reports the same fact as a value");
        Note("RelationTo('{0}') = 0x{1:X4}   <- what Listen could not return", ServerAddr, rel);

        Scratch.Remove(sendFile, recvFile);

        Log("MAIN", "shutdown begin");
        client.Dispose();
        server.Dispose();
        net.Dispose();
        rebuilt.Dispose();
        source.Dispose();

        return Verdict();
    }

    static bool ByteEqual (byte[] a, byte[] b)
    {
        if (a == null || b == null || a.Length != b.Length) return false;
        for (int i = 0; i < a.Length; ++i) if (a[i] != b[i]) return false;
        return true;
    }
}
