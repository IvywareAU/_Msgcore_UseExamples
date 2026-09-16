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
// P2PBridge.cs -- the TargetCom half of the two networked harnesses.
//
// The C# counterpart of ComExamples\common\P2PBridge.h, and
// duplicated from _Targetcore_UseExamples\dotNetExamples\common\TargetComInterop.cs for the same
// reason that one is: a tree of examples that cannot be built without a sibling
// tree of examples is not an example of anything. Only the members
// WsaStoreTest and WsaQueryTest use are declared -- this is a slice, not the
// type library.
//
// Those two harnesses are the only place in this tree where the two tiers meet:
// MsgcoreCom for what is IN a message, TargetCom for moving it. The mesh is
// WsaMeshTest's -- two P2PeerHubs in ONE PROCESS, each on its own pump thread
// inside the facade, joined by a loopback TCP connection. For anything about
// the mesh itself read ..\..\..\_Targetcore_UseExamples\ArchitectureFAQ.md.
//
// VTABLE ORDER MATTERS HERE TOO, and the slice is not an excuse: a subset is
// only safe because it is a PREFIX. Every member below appears at the slot the
// full interface gives it, and the ones this tree does not call are still
// declared where they fall. Drop one and everything after it shifts.
//
// THE IID OF IP2PHubCom WAS REISSUED for facade ABI 4, when eight typed
// Listen*/Connect* methods collapsed onto one endpoint pair. Leave a stale copy
// of this file against a current server and the cast fails loudly at
// QueryInterface instead of calling Listen with an int where a BSTR belongs.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

namespace P2P
{
    /// <summary>One P2P messaging endpoint. Wraps p2pf::IP2PHub.</summary>
    [ComImport]
    [Guid("12D65CF2-3F8C-4856-A52F-1B0B35F3B4FE")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IP2PHubCom
    {
        // `endpoint` is "tcp://:7788" or "tcp://127.0.0.1:7788"; a LISTEN must
        // not name a host, because the kernel binds INADDR_ANY regardless.
        //
        // These two are the only members that can answer a SUCCESS code other
        // than S_OK -- P2PF_S_UNRELATED_LINK, "armed, but these two addresses
        // are neither ancestor nor descendant, so nothing can be ROUTED through
        // the edge". A `void` signature discards a successful HRESULT, so a
        // .NET caller never sees it. The same fact arrives on OnError, which is
        // how WsaStoreTest observes it.
        [DispId(1)] void Listen  ([MarshalAs(UnmanagedType.BStr)] string toPeer,
                                  [MarshalAs(UnmanagedType.BStr)] string endpoint);
        [DispId(2)] void Connect ([MarshalAs(UnmanagedType.BStr)] string toPeer,
                                  [MarshalAs(UnmanagedType.BStr)] string endpoint);

        [DispId(3)] void Send     ([MarshalAs(UnmanagedType.BStr)]   string dest,
                                   [MarshalAs(UnmanagedType.BStr)]   string topic,
                                   [MarshalAs(UnmanagedType.Struct)] object payload);
        [DispId(4)] void SendText ([MarshalAs(UnmanagedType.BStr)] string dest,
                                   [MarshalAs(UnmanagedType.BStr)] string topic,
                                   [MarshalAs(UnmanagedType.BStr)] string text);

        [DispId(5)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool Broadcast ([MarshalAs(UnmanagedType.BStr)]   string topic,
                        [MarshalAs(UnmanagedType.Struct)] object payload);

        string Address { [DispId(6)] [return: MarshalAs(UnmanagedType.BStr)] get; }

        [DispId(7)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool IsPeerUp ([MarshalAs(UnmanagedType.BStr)] string peer);

        [DispId(8)] void Close ();

        // The read side, appended under an unchanged IID. RelationTo is how a
        // .NET caller learns what Listen could not tell it.
        [DispId(9)]  int RelationTo ([MarshalAs(UnmanagedType.BStr)] string peer);
        int ConCount { [DispId(10)] get; }
        [DispId(11)] [return: MarshalAs(UnmanagedType.BStr)] string PeerAt (int index);
        [DispId(12)] [return: MarshalAs(UnmanagedType.BStr)] string EndpointFor ([MarshalAs(UnmanagedType.BStr)] string peer);
        string Description { [DispId(13)] [return: MarshalAs(UnmanagedType.BStr)] get; }

        // --- slots 14-24: declared to be walked past ---------------------------
        //
        // WsaQueryTest needs MsgTag (slot 23) and SendEx (slot 25). A subset of
        // an interface is only safe as a PREFIX, so everything between the read
        // side and SendEx is declared here even though nothing in this tree
        // calls it. Delete one of these and SendEx lands on BroadcastEx, whose
        // first two BSTRs happen to line up -- so it would not crash, it would
        // broadcast the reply to every peer instead of answering the asker.
        [DispId(14)] void Disconnect ([MarshalAs(UnmanagedType.BStr)] string peer);
        [DispId(15)] int  SetTimer (int delayMillisecs, int key);
        [DispId(16)] void KillTimer (int timerId);
        [DispId(17)] int  Ping ([MarshalAs(UnmanagedType.BStr)] string peer, int timeoutMillisecs);
        [DispId(18)] void SetConOption ([MarshalAs(UnmanagedType.BStr)] string peer, int option, int value);
        [DispId(19)] int  GetConOption ([MarshalAs(UnmanagedType.BStr)] string peer, int option);
        [DispId(20)] void CloseIdleCons ();

        // --- the per-message envelope, valid only INSIDE an OnMessage handler --
        //
        // The kernel carries these beside the payload, so a reply can be matched
        // to its request without either of them spending a byte of payload on
        // saying which. Read anywhere else they answer p2pfNoMessage.
        string MsgDest     { [DispId(21)] [return: MarshalAs(UnmanagedType.BStr)] get; }
        int    MsgPriority { [DispId(22)] get; }
        int    MsgTag      { [DispId(23)] get; }
        int    MsgFlags    { [DispId(24)] get; }

        /// <summary>Send with an explicit correlation tag. Slot 25.</summary>
        [DispId(25)] void SendEx ([MarshalAs(UnmanagedType.BStr)]   string dest,
                                  [MarshalAs(UnmanagedType.BStr)]   string topic,
                                  [MarshalAs(UnmanagedType.Struct)] object payload,
                                  int tag, int priority, int flags);
    }

    /// <summary>The one init object. Wraps p2pf::IP2PNetwork.</summary>
    [ComImport]
    [Guid("319ECAAE-00D7-4B11-AA4D-94BF2934F5F8")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IP2PNetworkCom
    {
        [DispId(1)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IP2PHubCom CreateHub ([MarshalAs(UnmanagedType.BStr)] string address);

        string VersionString { [DispId(2)] [return: MarshalAs(UnmanagedType.BStr)] get; }
        int    MaxPayload    { [DispId(3)] get; }
    }

    /// <summary>The source dispinterface, marshalled into the sink's apartment.</summary>
    [ComImport]
    [Guid("5EFF43D2-F741-489E-B090-612904CC3CA8")]
    [InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
    public interface IP2PHubEvents
    {
        [DispId(1)] void OnMessage  ([MarshalAs(UnmanagedType.BStr)]        string source,
                                     [MarshalAs(UnmanagedType.BStr)]        string topic,
                                     [MarshalAs(UnmanagedType.Struct)]      object payload,
                                     [MarshalAs(UnmanagedType.VariantBool)] bool   broadcast);
        [DispId(2)] void OnPeerUp   ([MarshalAs(UnmanagedType.BStr)] string peer);
        [DispId(3)] void OnPeerDown ([MarshalAs(UnmanagedType.BStr)] string peer);
        [DispId(4)] void OnError    ([MarshalAs(UnmanagedType.BStr)] string what);
    }

    /// <summary>coclass TargetCom.P2PNetwork.</summary>
    [ComImport]
    [Guid("AC8A3AD3-F73E-467C-952C-AED2762C00C3")]
    public class P2PNetworkClass { }

    /// <summary>What a topic handler receives, payload already a byte[].</summary>
    public sealed class Message
    {
        public string Source;
        public string Topic;
        public byte[] Payload;
        public bool   Broadcast;

        public int Size { get { return Payload == null ? 0 : Payload.Length; } }

        /// <summary>The payload read as the NUL-terminated UTF-16 SendText puts on the wire.</summary>
        public string Text
        {
            get
            {
                if (Payload == null || Payload.Length < 2) return string.Empty;
                string s = Encoding.Unicode.GetString(Payload);
                int z = s.IndexOf('\0');
                return z >= 0 ? s.Substring(0, z) : s;
            }
        }
    }

    /// <summary>
    /// The sink. An ordinary class implementing an ordinary interface -- the
    /// same 100 lines of hand-written IDispatch that P2PBridge.h needs, and the
    /// same ClassInterface(None) rule as Msgc.StoreEvents: with the default
    /// AutoDispatch the DISPIDs would resolve against a generated class
    /// interface and every event would be misrouted in silence.
    ///
    /// Handlers run on the facade's dispatch thread, one per hub, concurrently
    /// with main -- a managed CCW is agile, so no proxy marshals them back.
    /// </summary>
    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    public sealed class HubSink : IP2PHubEvents
    {
        public readonly Dictionary<string, Action<Message>> Topics =
            new Dictionary<string, Action<Message>>(StringComparer.Ordinal);

        public Action<Message> Fallback;
        public Action<string>  PeerUp, PeerDown, Error;

        public void OnMessage (string source, string topic, object payload, bool broadcast)
        {
            var m = new Message { Source = source ?? string.Empty,
                                  Topic  = topic  ?? string.Empty,
                                  Payload = payload as byte[],
                                  Broadcast = broadcast };
            Action<Message> h;
            if (Topics.TryGetValue(m.Topic, out h) && h != null) h(m);
            else if (Fallback != null)                           Fallback(m);
        }

        public void OnPeerUp   (string peer) { if (PeerUp   != null) PeerUp  (peer ?? string.Empty); }
        public void OnPeerDown (string peer) { if (PeerDown != null) PeerDown(peer ?? string.Empty); }
        public void OnError    (string what) { if (Error    != null) Error   (what ?? string.Empty); }
    }

    public static class Endpoint
    {
        public static string TcpListen (int port)              { return "tcp://:" + port; }
        public static string TcpDial   (string host, int port) { return "tcp://" + host + ":" + port; }
    }

    /// <summary>IP2PHubCom plus its connection point.</summary>
    public sealed class Hub : IDisposable
    {
        private IP2PHubCom       m_hub;
        private IConnectionPoint m_cp;
        private HubSink          m_sink;
        private int              m_cookie;

        internal Hub (IP2PHubCom hub)
        {
            m_hub  = hub;
            m_sink = new HubSink();

            var cpc  = (IConnectionPointContainer)hub;
            var diid = typeof(IP2PHubEvents).GUID;
            cpc.FindConnectionPoint(ref diid, out m_cp);
            m_cp.Advise(m_sink, out m_cookie);
        }

        public IP2PHubCom Raw { get { return m_hub; } }

        public Hub OnTopic    (string topic, Action<Message> h) { m_sink.Topics[topic] = h; return this; }
        public Hub OnPeerUp   (Action<string> h)                { m_sink.PeerUp   = h;      return this; }
        public Hub OnPeerDown (Action<string> h)                { m_sink.PeerDown = h;      return this; }
        public Hub OnError    (Action<string> h)                { m_sink.Error    = h;      return this; }

        public int Listen   (string toPeer, string ep) { return Msgc.Api.Call(() => m_hub.Listen (toPeer, ep)); }
        public int Connect  (string toPeer, string ep) { return Msgc.Api.Call(() => m_hub.Connect(toPeer, ep)); }
        public int SendText (string dst, string topic, string text)
                                                       { return Msgc.Api.Call(() => m_hub.SendText(dst, topic, text)); }
        public int Send     (string dst, string topic, byte[] payload)
                                                       { return Msgc.Api.Call(() => m_hub.Send(dst, topic, payload)); }

        /// <summary>Send with a correlation tag, for a request/reply service.</summary>
        public int SendEx (string dst, string topic, object payload, int tag)
        {
            return Msgc.Api.Call(() => m_hub.SendEx(dst, topic, payload, tag, -1, 0));
        }

        /// <summary>
        /// The tag of the message being handled. Valid ONLY inside an
        /// OnMessage handler; anywhere else the facade answers p2pfNoMessage
        /// and this reads 0.
        /// </summary>
        public int MsgTag { get { return Msgc.Api.Try(() => m_hub.MsgTag); } }

        public bool IsPeerUp (string peer) { return Msgc.Api.Try(() => m_hub.IsPeerUp(peer)); }
        public string Address { get { return Msgc.Api.Try(() => m_hub.Address, string.Empty); } }

        public void Dispose ()
        {
            if (m_cp != null)
            {
                if (m_cookie != 0) { Msgc.Api.Call(() => m_cp.Unadvise(m_cookie)); m_cookie = 0; }
                Marshal.ReleaseComObject(m_cp); m_cp = null;
            }
            if (m_hub != null)
            {
                Msgc.Api.Call(() => m_hub.Close());
                Marshal.ReleaseComObject(m_hub); m_hub = null;
            }
            m_sink = null;
        }
    }

    /// <summary>CoCreateInstance(TargetCom.P2PNetwork) and the hub factory.</summary>
    public sealed class Network : IDisposable
    {
        private IP2PNetworkCom m_net;

        private Network (IP2PNetworkCom net) { m_net = net; }

        public static Network Create (out int hr)
        {
            IP2PNetworkCom net = null;
            hr = Msgc.Api.Call(() => net = (IP2PNetworkCom)(object)new P2PNetworkClass());
            return (net == null) ? null : new Network(net);
        }

        public Hub CreateHub (string address, out int hr)
        {
            IP2PHubCom raw = null;
            hr = Msgc.Api.Call(() => raw = m_net.CreateHub(address));
            if (raw == null) return null;
            Hub h = null;
            hr = Msgc.Api.Call(() => h = new Hub(raw));
            return h;
        }

        public string VersionString { get { return Msgc.Api.Try(() => m_net.VersionString, string.Empty); } }
        public int    MaxPayload    { get { return Msgc.Api.Try(() => m_net.MaxPayload); } }

        public void Dispose ()
        {
            if (m_net != null) { Marshal.ReleaseComObject(m_net); m_net = null; }
        }
    }
}
