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
// ComHarness.cs
//
// The C# counterpart of ComExamples\common\ComHarness.h.
//
// THE INTERESTING THING ABOUT THIS FILE IS HOW LITTLE OF IT THERE IS. The C++
// one is 1356 lines. Roughly a thousand of them are a BSTR wrapper, a VARIANT
// wrapper with eight typed readers, a COM smart pointer, an IEnumVARIANT loop,
// and typed facades over all nine interfaces -- so that a harness can write
// `f.asText()` instead of a VariantChangeType dance. None of that exists here,
// because the marshaller does it: `field.Value` already IS an object of the
// right CLR type, `field.Name` already IS a string, and lifetime is the
// collector's problem.
//
// So this file is only the four things the CLR does NOT hand over:
//
//   1. Check / Section / Note / Verdict -- the same scoring the other trees
//      use, so the logs of all three diff cleanly.
//   2. Api.Call -- HRESULT instead of exception, with the CLR's rewriting
//      undone (read its comment; it is the trap of this tier).
//   3. PagingSink and StoreEvents -- two sink objects that are ordinary classes
//      implementing ordinary interfaces. The C++ tree needs a hand-written
//      IDispatch with a switch over DISPIDs for the second one.
//   4. Each() -- one IEnumVARIANT loop, because a [ComImport] interface with a
//      DISPID_NEWENUM member is still not IEnumerable to C#.
//
// APARTMENT AND THREADING, and it is NOT the same as the C++ tree.
//
// Every harness here is [STAThread]. The C++ COM harnesses declare an STA too
// and every OnChange arrives on the main thread, because a C++ IDispatch sink
// is apartment-bound: MsgcoreCom parks it in the Global Interface Table,
// re-fetches it on its own MTA dispatch thread and gets back a PROXY that
// marshals into the client's apartment.
//
// A MANAGED SINK IS AGILE. The CLR's CCW aggregates the free-threaded
// marshaler, so the same GIT re-fetch hands the dispatch thread the identical
// pointer -- no proxy, no apartment transition. OnChange therefore arrives ON
// THE DISPATCH THREAD here, concurrently with main. Every counter a handler
// touches below is Interlocked for that reason.
//
// The pump stays anyway. It costs nothing, Gate.Wait needs it, and it is
// load-bearing the moment any non-agile object enters the picture -- which is a
// change a client can make by accident.
//
// THE PAGING SINK IS THE OPPOSITE CASE and does not care about any of this: it
// is called synchronously, on the calling thread, before the call that
// provoked it returns. Agility is what makes that legal at all.
//
// Exit-code contract, identical to the C++ trees:
//   0 = SUCCESS   1 = SETUP (server not registered)   3 = a check failed

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Threading;

using MsgcoreCom;

namespace Msgc
{
    // =======================================================================
    // Scoring and output. Same shapes and same wording as the C++ harness, so
    // logs\Debug\*.txt of the two trees can be diffed line for line.
    // =======================================================================
    public static class Harness
    {
        public const int ExitSuccess = 0;
        public const int ExitSetup   = 1;
        public const int ExitFail    = 3;

        public static int Checks { get; private set; }
        public static int Failed { get; private set; }

        private static readonly object s_out = new object();

        [DllImport("kernel32.dll")]
        private static extern uint GetCurrentThreadId ();

        public static uint Tid { get { return GetCurrentThreadId(); } }
        public static uint MainTid { get; private set; }

        public static void InitConsole ()
        {
            MainTid = GetCurrentThreadId();
            try { Console.OutputEncoding = new UTF8Encoding(false); }
            catch (Exception) { /* stdout is a pipe with no console: leave it */ }
        }

        /// <summary>
        /// One scored expectation. The C++ tree passes __LINE__; here the
        /// compiler can be asked for it, so a failure names its own line
        /// without every call site carrying a macro.
        /// </summary>
        public static void Check (bool ok, string what,
                                  [System.Runtime.CompilerServices.CallerLineNumber] int line = 0)
        {
            Checks++;
            if (ok) return;
            Failed++;
            lock (s_out) { Console.WriteLine("  FAIL (line {0}): {1}", line, what); Console.Out.Flush(); }
        }

        public static void Section (string title)
        {
            lock (s_out) { Console.WriteLine("\n--- {0}", title); Console.Out.Flush(); }
        }

        public static void Note (string format, params object[] args)
        {
            string s = (args == null || args.Length == 0) ? format : string.Format(format, args);
            lock (s_out) { Console.WriteLine("  " + s); Console.Out.Flush(); }
        }

        public static void Log (string role, string format, params object[] args)
        {
            var now = DateTime.Now;
            string body = (args == null || args.Length == 0) ? format : string.Format(format, args);
            string line = string.Format("[{0:00}:{1:00}:{2:00}.{3:000} tid={4} {5}] {6}",
                                        now.Hour, now.Minute, now.Second, now.Millisecond,
                                        GetCurrentThreadId(), role, body);
            lock (s_out) { Console.WriteLine(line); Console.Out.Flush(); }
        }

        /// <summary>
        /// Print an HRESULT with its Msgcore name and the sentence that came
        /// with it.
        ///
        /// The sentence is Api.LastMessage, which is simply the exception's
        /// Message -- the CLR reads IErrorInfo for us. The C++ tree calls
        /// GetErrorInfo and manages the BSTR to reach the same string.
        ///
        /// One asymmetry worth seeing in the logs: where the CLR REWROTE the
        /// HRESULT (E_INVALIDARG -> ArgumentException) the sentence is the
        /// CLR's own, not the server's, because the rewrite happens before the
        /// IErrorInfo is read.
        /// </summary>
        public static void ShowError (string what, int hr)
        {
            string s = Api.LastMessage ?? string.Empty;
            s = s.Replace("\r", " ").Replace("\n", " ");
            if (s.Length > 160) s = s.Substring(0, 157) + "...";
            lock (s_out)
            {
                Console.WriteLine("  {0} -> 0x{1:X8} {2}", what, hr, Hr.Name(hr));
                if (s.Length != 0) Console.WriteLine("     {0}", s);
                Console.Out.Flush();
            }
        }

        /// <summary>The COM server is not registered, or a dependency is missing.</summary>
        public static int SetupFailure (string what, int hr)
        {
            Log("MAIN", "SETUP: {0} failed (0x{1:X8} {2})", what, hr, Hr.Name(hr));
            if (hr == Hr.REGDB_E_CLASSNOTREG)
                Console.WriteLine("\nMsgcoreCom is not registered. Run:\n"
                                + "    run_all.ps1           (registers per-user, runs, unregisters)\n"
                                + "or  regsvr32 /n /i:user \"...\\out\\x64\\Debug\\MsgcoreCom.dll\"");
            return ExitSetup;
        }

        public static int Verdict ()
        {
            int exit = (Failed == 0) ? ExitSuccess : ExitFail;
            lock (s_out)
            {
                Console.WriteLine("\n{0} checks, {1} failed. Done (exit={2}).", Checks, Failed, exit);
                Console.Out.Flush();
            }
            return exit;
        }
    }

    // =======================================================================
    // The call boundary.
    // =======================================================================
    public static class Api
    {
        /// <summary>
        /// The sentence that came with the last failure. Set by every Call
        /// overload, read by Harness.ShowError.
        /// </summary>
        public static string LastMessage { get; private set; }

        /// <summary>
        /// Run a call and hand back its HRESULT instead of letting it throw.
        ///
        /// THE CATCH IS Exception, NOT COMException, and that is the single
        /// most important line in this file.
        ///
        /// These interop declarations are not [PreserveSig], so a failed call
        /// raises. But the CLR does not raise a COMException for every failed
        /// COM call: it rewrites a fixed table of well-known HRESULTs into CLR
        /// exception types first. Measured against this very server:
        ///
        ///     msgcDetached  0x8004030B -> COMException        (custom, passes through)
        ///     msgcNotList   0x80040307 -> COMException
        ///     msgcRange     0x8004030A -> COMException
        ///     E_INVALIDARG  0x80070057 -> ArgumentException   (REWRITTEN)
        ///     E_POINTER     0x80004003 -> NullReferenceException
        ///
        /// So a .NET client that writes `catch (COMException)` around this API
        /// is correct for Msgcore's own 0x8004030x range and CRASHES on the
        /// argument validation the IDL added for automation clients -- which is
        /// precisely the validation a script is most likely to trip.
        /// Marshal.GetHRForException recovers the original code from whichever
        /// type the CLR chose.
        /// </summary>
        public static int Call (Action action)
        {
            LastMessage = string.Empty;
            try { action(); return Hr.S_OK; }
            catch (Exception e) { LastMessage = e.Message; return Marshal.GetHRForException(e); }
        }

        /// <summary>The same, for a call that answers a value.</summary>
        public static int Call<T> (Func<T> f, out T value)
        {
            LastMessage = string.Empty;
            value = default(T);
            try { value = f(); return Hr.S_OK; }
            catch (Exception e) { LastMessage = e.Message; return Marshal.GetHRForException(e); }
        }

        /// <summary>
        /// For reads where a failure is not interesting -- the shape the C++
        /// harness's accessors have, where a broken object answers a default
        /// rather than propagating.
        /// </summary>
        public static T Try<T> (Func<T> f, T fallback = default(T))
        {
            try { return f(); } catch (Exception e) { LastMessage = e.Message; return fallback; }
        }

        /// <summary>
        /// Walk a DISPID_NEWENUM result.
        ///
        /// A [ComImport] interface with a member called _NewEnum is not
        /// IEnumerable to C#, and the CLR only wires foreach to COM
        /// collections through a TlbImp-generated wrapper that this tree does
        /// not have. So the IEnumVARIANT loop is written once, here, and every
        /// collection in the server goes through it:
        ///
        ///     foreach (var v in Api.Each(list._NewEnum())) ...
        ///
        /// Every _NewEnum in MsgcoreCom is a SNAPSHOT, so a body may delete as
        /// it walks -- which the underlying cursor, being a live position,
        /// would not survive.
        ///
        /// IT TAKES TWO SHAPES, and finding that out is worth the branch below.
        /// Called through a [ComImport] interface, _NewEnum answers exactly
        /// what the vtable said -- an IUnknown that QIs to IEnumVARIANT.
        /// Called LATE-BOUND, through Type.InvokeMember, the CLR does not hand
        /// the raw enumerator back at all: it substitutes
        /// CustomMarshalers.EnumeratorViewOfEnumVariant, a managed
        /// System.Collections.IEnumerator ADAPTER over it -- which is not an
        /// IEnumVARIANT and would silently enumerate nothing if this only
        /// looked for one. (That substitution is also what makes VB.NET's
        /// `For Each` work over an arbitrary COM collection.)
        /// </summary>
        public static IEnumerable<object> Each (object newEnum)
        {
            // The late-bound shape first: the CLR already did the work.
            var adapter = newEnum as System.Collections.IEnumerator;
            if (adapter != null)
            {
                while (adapter.MoveNext()) yield return adapter.Current;
                yield break;
            }

            var seq = newEnum as System.Collections.IEnumerable;
            if (seq != null)
            {
                foreach (var o in seq) yield return o;
                yield break;
            }

            var e = newEnum as IEnumVARIANT;
            if (e == null) yield break;

            object[] one = new object[1];
            IntPtr fetched = Marshal.AllocCoTaskMem(IntPtr.Size);
            try
            {
                while (e.Next(1, one, fetched) == Hr.S_OK)
                {
                    if (Marshal.ReadInt32(fetched) == 0) break;
                    yield return one[0];
                }
            }
            finally { Marshal.FreeCoTaskMem(fetched); }
        }

        /// <summary>A BLOB16 node reads back as a byte[] inside the VARIANT.</summary>
        public static byte[] AsBytes (object v)
        {
            byte[] b = v as byte[];
            return b ?? new byte[0];
        }

        /// <summary>Bytes for a BLOB16 write: a byte[] marshals as SAFEARRAY(VT_UI1).</summary>
        public static byte[] Bytes (params int[] values)
        {
            var b = new byte[values.Length];
            for (int i = 0; i < values.Length; ++i) b[i] = (byte)values[i];
            return b;
        }
    }

    // =======================================================================
    // The STA pump, and a counter whose wait pumps.
    // =======================================================================
    public static class Pump
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct MSG
        {
            public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam;
            public uint time; public int ptX; public int ptY;
        }

        private const uint PM_REMOVE = 0x0001;

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool PeekMessageW (out MSG msg, IntPtr hWnd, uint min, uint max, uint remove);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool TranslateMessage (ref MSG msg);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr DispatchMessageW (ref MSG msg);

        public static void Once ()
        {
            MSG msg;
            while (PeekMessageW(out msg, IntPtr.Zero, 0, 0, PM_REMOVE))
            {
                TranslateMessage(ref msg);
                DispatchMessageW(ref msg);
            }
        }

        /// <summary>Pump for a fixed span -- used to observe that something did NOT arrive.</summary>
        public static void For (int ms)
        {
            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < ms) { Once(); Thread.Sleep(5); }
        }
    }

    /// <summary>A counter a handler bumps and the main thread waits on, pumping.</summary>
    public sealed class Gate
    {
        private int m_count;

        public void Bump () { Interlocked.Increment(ref m_count); }
        public void Reset () { Interlocked.Exchange(ref m_count, 0); }
        public int  Count { get { return Interlocked.CompareExchange(ref m_count, 0, 0); } }

        public bool Wait (int want, int ms)
        {
            var sw = Stopwatch.StartNew();
            for (;;)
            {
                Pump.Once();
                if (Count >= want) return true;
                if (sw.ElapsedMilliseconds > ms) return false;
                Thread.Sleep(5);
            }
        }
    }

    // =======================================================================
    // The two sinks.
    //
    // ClassInterface(None) is load-bearing on BOTH. With the default
    // AutoDispatch the CCW would expose an auto-generated class interface as
    // its IDispatch, and a dispinterface's DISPIDs would resolve against THAT
    // -- events silently misrouted. With None the CCW's dispatch identity is
    // the declared interface and the DispIds line up.
    //
    // Its measured cost: QueryInterface(IID_IDispatch) on this CCW returns
    // E_NOINTERFACE, and only QueryInterface(DIID__IMsgStoreEvents) succeeds.
    // A managed sink is therefore connectable only because CMsgStore::Advise
    // tries the DIID after IID_IDispatch fails (ComStore.cpp:1253). A
    // connection point that asked for IID_IDispatch alone would refuse every
    // C# client -- and the same is true one tier up, in TargetCom.
    // =======================================================================

    /// <summary>
    /// The queued, asynchronous side: OnChange and OnError.
    ///
    /// Handlers run on the DLL's dispatch thread (see the header), so anything
    /// they touch has to be safe for that. The harnesses keep to Interlocked
    /// counters and a locked list.
    /// </summary>
    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    public sealed class StoreEvents : IMsgStoreEvents
    {
        public sealed class Change
        {
            public int    Kind;
            public long   P2Pos;
            public string Path;
            public uint   Tid;
        }

        private readonly List<Change> m_seen = new List<Change>();
        private readonly List<string> m_errors = new List<string>();
        private readonly object       m_lock = new object();

        public readonly Gate Arrived = new Gate();

        /// <summary>
        /// Extra work to run inside the handler. Used by MgrPersistTest to read
        /// back into the store from within OnChange -- the case that would
        /// deadlock if the flat sink were called through directly, and the
        /// reason the events are queued at all.
        /// </summary>
        public Action<Change> Hook;

        public void OnChange (int kind, long p2pos, string path)
        {
            var c = new Change { Kind = kind, P2Pos = p2pos,
                                 Path = path ?? string.Empty, Tid = Harness.Tid };
            lock (m_lock) m_seen.Add(c);

            var hook = Hook;
            if (hook != null) hook(c);

            Arrived.Bump();
        }

        public void OnError (string what)
        {
            lock (m_lock) m_errors.Add(what ?? string.Empty);
        }

        public Change[] Seen   { get { lock (m_lock) return m_seen.ToArray(); } }
        public string[] Errors { get { lock (m_lock) return m_errors.ToArray(); } }

        public void Clear ()
        {
            lock (m_lock) { m_seen.Clear(); m_errors.Clear(); }
            Arrived.Reset();
        }

        public int CountOf (MsgTriggerFlag kind)
        {
            int n = 0;
            foreach (var c in Seen) if (c.Kind == (int)kind) n++;
            return n;
        }
    }

    /// <summary>
    /// The synchronous side: paging.
    ///
    /// Every method here runs on the thread that provoked it, under the store
    /// lock, with the core blocked waiting for the answer. Returning False is
    /// a refusal and is reported to that caller; THROWING is not an option the
    /// core can use, because it cannot unwind through a page fault -- so this
    /// class never does.
    ///
    /// It is deliberately trivial. A handler that blocks stalls the store, and
    /// one that re-enters the store beyond the subtree it was asked for
    /// deadlocks on the lock it is already under.
    /// </summary>
    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    public sealed class PagingSink : IMsgPagingSink
    {
        private int m_ins, m_outs;

        /// <summary>What every callback answers. False to make the store report a refusal.</summary>
        public bool Answer = true;

        public object LastPos   { get; private set; }
        public bool   LastFlush { get; private set; }
        public uint   LastTid   { get; private set; }

        public int PageIns   { get { return Interlocked.CompareExchange(ref m_ins,  0, 0); } }
        public int PageOuts  { get { return Interlocked.CompareExchange(ref m_outs, 0, 0); } }

        public void Reset ()
        {
            Interlocked.Exchange(ref m_ins,  0);
            Interlocked.Exchange(ref m_outs, 0);
            LastPos = null; LastFlush = false; LastTid = 0;
        }

        public bool OnPageIn (object p2pos)
        {
            Interlocked.Increment(ref m_ins);
            LastPos = p2pos; LastTid = Harness.Tid;
            return Answer;
        }

        public bool OnPageOut (object p2pos, bool flush)
        {
            Interlocked.Increment(ref m_outs);
            LastPos = p2pos; LastFlush = flush; LastTid = Harness.Tid;
            return Answer;
        }

        // OnPopulate is gone with the interface member: it was the core's third
        // paging callback, it is not exposed below this tier, and a sink that
        // still declared it would have the wrong vtable.
    }

    // =======================================================================
    // Store -- CoCreateInstance(MsgcoreCom.MsgStore), plus the connection
    // point, so a harness never repeats the Advise/Unadvise dance.
    // =======================================================================
    public sealed class Store : IDisposable
    {
        private IConnectionPoint m_cp;
        private int              m_cookie;

        private Store (IMsgStoreCom com) { Com = com; }

        /// <summary>The interface itself. Harnesses call through this.</summary>
        public IMsgStoreCom Com { get; private set; }

        /// <summary>Non-null once Sink() has been called.</summary>
        public StoreEvents Events { get; private set; }

        public IMsgFieldCom Root { get { return Com.Root; } }

        /// <summary>
        /// Each CoCreateInstance is its own store: a store IS a document, and
        /// nothing about two of them interferes. (TargetCom's P2PNetwork is the
        /// other way round -- one process-wide singleton.)
        /// </summary>
        public static Store Create (out int hr)
        {
            IMsgStoreCom com = null;
            hr = Api.Call(() => com = (IMsgStoreCom)(object)new MsgStoreClass());
            return Hr.Failed(hr) ? null : new Store(com);
        }

        /// <summary>Attach a StoreEvents sink to the connection point.</summary>
        public StoreEvents Sink ()
        {
            if (Events != null) return Events;
            Events = new StoreEvents();

            var cpc  = (IConnectionPointContainer)Com;
            var diid = typeof(IMsgStoreEvents).GUID;
            cpc.FindConnectionPoint(ref diid, out m_cp);
            m_cp.Advise(Events, out m_cookie);
            return Events;
        }

        /// <summary>
        /// Drop the connection. After this nothing is delivered at all, which
        /// is the other half of the connection-point contract.
        /// </summary>
        public int Unsink ()
        {
            if (m_cp == null || m_cookie == 0) return Hr.S_OK;
            int hr = Api.Call(() => m_cp.Unadvise(m_cookie));
            m_cookie = 0;
            return hr;
        }

        public void Dispose ()
        {
            if (m_cp != null)
            {
                if (m_cookie != 0) { Api.Call(() => m_cp.Unadvise(m_cookie)); m_cookie = 0; }
                Marshal.ReleaseComObject(m_cp);
                m_cp = null;
            }
            if (Com != null)
            {
                Api.Call(() => Com.Close());
                Marshal.ReleaseComObject(Com);
                Com = null;
            }
            Events = null;
        }
    }

    // =======================================================================
    // Scratch files. Every harness that saves writes beside its own exe and
    // cleans up, so two configurations can run concurrently without colliding.
    // =======================================================================
    public static class Scratch
    {
        public static string Path (string leaf)
        {
            string dir = AppDomain.CurrentDomain.BaseDirectory;
            return System.IO.Path.Combine(dir, leaf);
        }

        public static void Remove (params string[] paths)
        {
            foreach (var p in paths)
                try { if (System.IO.File.Exists(p)) System.IO.File.Delete(p); }
                catch (Exception) { /* a locked scratch file is not a test failure */ }
        }
    }
}
