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
// MgrCApiTest.cs
//
// The counterpart of ..\ComExamples\MgrCApiTestCom, and the
// one harness in this tree that does not use common\MsgcoreComInterop.cs at all.
//
// THE SUBJECT IS THE SECOND ABI. Every interface in MsgcoreCom is `dual`, so
// the same objects can be reached by VTABLE -- which the other seven harnesses
// do, through the [ComImport] declarations -- or LATE-BOUND through
// IDispatch::Invoke, which is the only way VBScript, PowerShell, VBA and
// classic ASP can reach them at all.
//
// Not one line below names a MsgcoreCom interface, coclass or IID. The store is
// created from its PROGID, and every operation goes through InvokeMember by
// NAME -- so the [ComImport] declarations the other seven harnesses depend on
// are not merely unused here, they are absent from the binding entirely. That is
// not a curiosity: it is exactly what PowerShell does for
// `New-Object -ComObject MsgcoreCom.MsgStore`, so what passes here is what
// ..\ComExamples\script\ps_client.ps1 can do -- and this
// harness is where that claim is checked in a language with an exit code.
//
// FOUR THINGS ARE DIFFERENT THROUGH THIS FACE:
//
//   1. Failure arrives WRAPPED. The C++ harness reads EXCEPINFO.scode off the
//      Invoke; the CLR reads it for you, but buries it one level down in a
//      TargetInvocationException. Section 6 measures the unwrapping, which is
//      the thing a .NET caller has to get right and PowerShell does for free.
//   2. A SUCCESS code other than S_OK does not survive. Section 6.
//   3. DISPID_VALUE makes a node read as its own value, and DISPID_NEWENUM
//      makes For Each work. Neither exists in the flat C ABI, because C has no
//      use for either. Section 3.
//   4. There is no type checking at the call site at all. A misspelled member
//      is a RUN-TIME MissingMemberException -- the price of late binding, and
//      the reason the type library is worth shipping. Section 1.
//
// Exit codes: 0 SUCCESS  1 SETUP  3 a check failed.

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

// Hr ONLY, and it is worth being exact about why that does not weaken the
// claim above: Hr is a table of integer constants naming the HRESULTs this
// server documents. It binds nothing, it is not generated from anything, and a
// script would have the same values written into it as literals. No interface,
// no coclass and no IID from common\MsgcoreComInterop.cs is named below.
using MsgcoreCom;
using Msgc;
using static Msgc.Harness;

static class MgrCApiTest
{
    // -------------------------------------------------------------------
    // Late binding by name, and nothing else.
    //
    // This is what a scripting host does for its user on every line, written
    // out once so the harness body reads like the script it stands for.
    //
    // NOTE WHAT IS *NOT* HERE, next to the C++ Disp class: no GetIDsOfNames, no
    // DISPPARAMS, no argument reversal (DISPPARAMS carries arguments backwards
    // -- the single most common mistake a hand-written IDispatch client makes),
    // no DISPID_PROPERTYPUT named argument, no EXCEPINFO to allocate and free.
    // The CLR's late binder does all of it. What it does NOT do is unwrap the
    // exception; see Err() below.
    // -------------------------------------------------------------------
    static object Get (object o, string name)
    {
        return o.GetType().InvokeMember(name, BindingFlags.GetProperty, null, o, null);
    }

    static void Put (object o, string name, object value)
    {
        o.GetType().InvokeMember(name, BindingFlags.SetProperty, null, o, new object[] { value });
    }

    static object Call (object o, string name, params object[] args)
    {
        return o.GetType().InvokeMember(name, BindingFlags.InvokeMethod, null, o, args);
    }

    /// <summary>
    /// A PARAMETERISED property get -- `coll.Item("name")` -- which is a
    /// different binding flag from Get() above and cannot be spelled with it.
    /// A collection's Item is a propget WITH an argument, which is what makes
    /// `coll("name")` work in VBScript and needs saying out loud in C#.
    /// </summary>
    static object GetAt (object o, string name, params object[] args)
    {
        return o.GetType().InvokeMember(name, BindingFlags.GetProperty, null, o, args);
    }

    /// <summary>
    /// What a script's Err object holds after a failed call: the code and the
    /// sentence.
    ///
    /// Getting here is the one piece of ceremony a .NET late-bound caller
    /// cannot avoid. Reflection wraps ANY exception the target raised in a
    /// TargetInvocationException, so the COMException that carries Msgcore's
    /// code is the InnerException -- and a client that inspects the OUTER one
    /// finds HResult 0x80131604 (the reflection wrapper) every single time, for
    /// every distinct failure. PowerShell unwraps it before it reaches the
    /// script; C# does not.
    /// </summary>
    sealed class ScriptError
    {
        public int    Number;        // Err.Number
        public string Description;   // Err.Description
        public Type   OuterType;
        public Type   InnerType;
    }

    static ScriptError Err (Action a)
    {
        try { a(); return null; }
        catch (Exception e)
        {
            Exception inner = (e is TargetInvocationException && e.InnerException != null)
                            ? e.InnerException : e;
            return new ScriptError {
                Number      = Marshal.GetHRForException(inner),
                Description = inner.Message ?? string.Empty,
                OuterType   = e.GetType(),
                InnerType   = inner.GetType()
            };
        }
    }

    static void ShowScriptError (string what, ScriptError e)
    {
        string s = (e.Description ?? string.Empty).Replace("\r", " ").Replace("\n", " ");
        if (s.Length > 150) s = s.Substring(0, 147) + "...";
        Console.WriteLine("  {0}\n     {1}({2}), Err.Number = 0x{3:X8} {4}",
                          what, e.OuterType.Name, e.InnerType.Name, e.Number, Hr.Name(e.Number));
        if (s.Length != 0) Console.WriteLine("     Err.Description = {0}", s);
        Console.Out.Flush();
    }


    // =====================================================================
    // 1. Reaching the store at all, by name
    // =====================================================================
    static void Demo_LateBinding (object store)
    {
        Section("1. Late binding -- a ProgID and InvokeMember, nothing else");

        // Every member is discoverable by NAME at run time. This is the whole
        // of what a script engine knows about the object before it calls it.
        var members = new[] { "RootName", "Filename", "Size", "IsValid", "VersionString" };
        foreach (var m in members)
            Check(Err(() => Get(store, m)) == null, "property '" + m + "' resolves by name");

        // Names are matched case-INSENSITIVELY -- the binder goes through
        // IDispatch::GetIDsOfNames, which is what lets VBScript write
        // `store.rootname` and VBA write `Store.RootName`.
        string lower = null, upper = null, mixed = null;
        Check(Err(() => lower = (string)Get(store, "rootname")) == null, "'rootname' resolves");
        Check(Err(() => upper = (string)Get(store, "ROOTNAME")) == null, "'ROOTNAME' resolves");
        Check(Err(() => mixed = (string)Get(store, "RootName")) == null, "'RootName' resolves");
        Check(lower == upper && upper == mixed, "and all three answer the same value");

        // AND A MISSPELLING IS A RUN-TIME ERROR, not a compile-time one. This
        // is the price of late binding, and the reason a type library is worth
        // shipping even to clients that will not use it for early binding.
        //
        // WHAT IT IS NOT is a MissingMemberException. Reflection raises that
        // for a managed type it can see the members of; against IDispatch it
        // has no member list of its own, so it forwards the name to
        // GetIDsOfNames and reports what came back -- DISP_E_UNKNOWNNAME, as a
        // plain COMException.
        var bad = Err(() => Get(store, "RootNmae"));
        Check(bad != null, "'RootNmae' fails");
        Check(bad.Number == Hr.DISP_E_UNKNOWNNAME,
              "with DISP_E_UNKNOWNNAME, straight from GetIDsOfNames");
        Note("'RootNmae' -> {0} 0x{1:X8} at run time, not at build time",
             bad.OuterType.Name, bad.Number);

        // AND IT IS NOT WRAPPED, which section 6 is the other half of. A
        // failure the BINDER hits -- resolving the name -- comes out as itself;
        // a failure the TARGET raises, after the name resolved, comes out
        // inside a TargetInvocationException. Two failures of one call, at two
        // different depths, and a client that unwraps unconditionally is wrong
        // about this one.
        Check(bad.OuterType == typeof(COMException),
              "and unwrapped -- a BINDER failure is not a target failure");

        Check(((string)Get(store, "VersionString")).Length != 0, "VersionString reads");
        Note("VersionString: {0}", Get(store, "VersionString"));
        Check((bool)Get(store, "IsValid"), "IsValid reads as a Boolean");
    }


    // =====================================================================
    // 2. Building a store through InvokeMember
    // =====================================================================
    static void Demo_BuildLateBound (object store)
    {
        Section("2. Declares and typed reads, all by name");

        object root = Get(store, "Root");
        Check(root != null, "Root answered an object");

        // Declare ( name, value, update ) -- three arguments, in declaration
        // order. The binder marshals each one into a VARIANT by its CLR type,
        // which is the whole reason the signature takes a VARIANT.
        object cfg = Call(root, "Declare", "cfg", 0, true);
        Check(cfg != null, "Declare answered the new node");

        var leaves = new[] {
            new { Name = "width",   Value = (object)1024,          Type = "INT32"  },
            new { Name = "scale",   Value = (object)1.5,           Type = "DOUBLE" },
            new { Name = "title",   Value = (object)"Chartboard",  Type = "WSTR16" },
            new { Name = "visible", Value = (object)true,          Type = "BOOL"   },
            new { Name = "big",     Value = (object)5000000000L,   Type = "INT64"  },
        };

        foreach (var leaf in leaves)
            Check(Err(() => Call(cfg, "Declare", leaf.Name, leaf.Value, true)) == null,
                  "declared '" + leaf.Name + "' late-bound");

        // Read them back, and check the DECLARED TYPE survived the trip through
        // two VARIANT conversions -- the CLR's on the way down, Msgcore's on the
        // way in.
        foreach (var leaf in leaves)
        {
            object child = Call(cfg, "Child", leaf.Name);
            Check((string)Get(child, "TypeName") == leaf.Type,
                  leaf.Name + " came back as " + leaf.Type);
        }

        // DeclareTyped, the one member a VARIANT cannot stand in for.
        Call(cfg, "DeclareTyped", "tiny", 250, 2 /* msgcTypeUInt08 */, true);
        object tiny = Call(cfg, "Child", "tiny");
        Check((string)Get(tiny, "TypeName") == "UINT08", "DeclareTyped set an explicit width");

        Check(Convert.ToInt32(Get(cfg, "Count")) == 6, "six children declared entirely through Invoke");
        Note("6 children declared and typed, entirely through InvokeMember");
    }


    // =====================================================================
    // 3. DISPID_VALUE and DISPID_NEWENUM -- what C had no use for
    // =====================================================================
    //
    // These two dispids are the whole reason a scripting client experiences
    // this object as a VALUE WITH CHILDREN rather than as a handle with
    // accessors. The flat C ABI has neither, and could not: C has nothing to
    // spell them with.
    //
    static void Demo_DefaultMemberAndForEach (object store)
    {
        Section("3. DISPID_VALUE and DISPID_NEWENUM -- the script-shaped members");

        object width = Call(store, "FieldAt", ".cfg.width");

        // `Value` is DISPID_VALUE (0), so naming it and naming NOTHING are the
        // same call -- which is what makes `Write-Host $store.FieldAt("cfg.width")`
        // print 1024 rather than a type name. The empty member name is how the
        // CLR's binder spells "the default member".
        object byName    = Get(width, "Value");
        object byDefault = width.GetType().InvokeMember("", BindingFlags.GetProperty,
                                                        null, width, null);
        Check(Convert.ToInt32(byName) == 1024, "Value reads 1024");
        Check(Convert.ToInt32(byDefault) == 1024, "and so does the default member");

        // A property PUT through the default member, which is `f = 1600` in VB.
        width.GetType().InvokeMember("", BindingFlags.SetProperty, null, width,
                                     new object[] { 1600 });
        Check(Convert.ToInt32(Get(width, "Value")) == 1600, "and writing through it lands");

        // DISPID_NEWENUM (-4) is what `For Each child In cfg` compiles to. It
        // is a RESTRICTED member, which keeps it out of a browser's member list
        // and does NOT keep it out of GetIDsOfNames -- so a late-bound caller
        // can still ask for it by name.
        object cfg = Call(store, "FieldAt", ".cfg");
        object newEnum = cfg.GetType().InvokeMember("_NewEnum", BindingFlags.GetProperty,
                                                    null, cfg, null);
        Check(newEnum != null, "_NewEnum resolves by name even though it is restricted");

        // AND IT DOES NOT COME BACK AS THE THING THE VTABLE RETURNED. The
        // early-bound harnesses get an IUnknown that QIs to IEnumVARIANT; here
        // the CLR substitutes a managed ADAPTER,
        // CustomMarshalers.EnumeratorViewOfEnumVariant, which is an ordinary
        // System.Collections.IEnumerator and is NOT an IEnumVARIANT. A client
        // that only knows the raw shape enumerates nothing, silently. (The same
        // substitution is what makes VB.NET's For Each work over any COM
        // collection.) Msgc.Api.Each in ComHarness.cs handles both.
        Check(newEnum is System.Collections.IEnumerator,
              "and the CLR substituted a managed IEnumerator adapter for it");
        Check(!(newEnum is System.Runtime.InteropServices.ComTypes.IEnumVARIANT),
              "which is NOT an IEnumVARIANT -- the raw shape is gone");
        Note("_NewEnum late-bound -> {0}", newEnum.GetType().Name);

        // Walk it exactly as a host does, and read each element's DEFAULT
        // member -- so this loop is `For Each c In cfg : total = total + c : Next`.
        int seen = 0;
        var names = new StringBuilder();
        foreach (var child in Api.Each(newEnum))
        {
            names.Append(Get(child, "Name")).Append(' ');
            ++seen;
        }
        Check(seen == 6, "For Each saw six children");
        Check(names.ToString() == "width scale title visible big tiny ", "in declaration order");
        Note("For Each over cfg: {0} children -- {1}", seen, names);
    }


    // =====================================================================
    // 4. The live/detached trap, and its removal
    // =====================================================================
    //
    // THIS SECTION USED TO DEMONSTRATE A TRAP AND NOW DEMONSTRATES ITS ABSENCE,
    // so it is worth saying what the trap was. Two families below this tier
    // answered the same handle type: one aliased the live tree, the other
    // deep-copied, and a write through the copy SUCCEEDED and reached nothing.
    // This server surfaced it as a readable IsLive and a trappable
    // msgcDetached, which was the best that could be done from up here.
    //
    // It cannot arise now. Every node is a ROUTE re-resolved per call, so Child
    // and Item and FieldAt all answer the same live thing, and Item / IsLive /
    // msgcDetached are gone rather than kept as ceremony. What is checked here
    // is that the trap is UNREACHABLE -- the removed members are not found by
    // name, which is exactly how a late-bound caller would discover it.
    //
    static void Demo_NoDetachedNodes (object store)
    {
        Section("4. Live vs detached -- the trap is gone, not merely trappable");

        object cfg     = Call(store, "FieldAt", ".cfg");
        object byChild = Call(cfg, "Child", "title");

        // The two members the trap was made of are simply not there.
        var eItem = Err(() => GetAt(cfg, "Item", "title"));
        Check(eItem != null, "Field.Item is gone");
        var eLive = Err(() => Get(byChild, "IsLive"));
        Check(eLive != null, "and Field.IsLive with it");
        Note("Both answer DISP_E_UNKNOWNNAME -- the CLR reports it as a missing member.");

        // The write reaches the store, as it does through every node here.
        Put(byChild, "Value", "Chartboard II");
        Check((string)Get(byChild, "Value") == "Chartboard II", "the write landed");

        // The SECOND route to the same node -- through the descendant
        // collection, whose Item was the other detached accessor -- reads the
        // write and can make one of its own.
        object desc    = Call(cfg, "Descendants", true);
        object viaColl = GetAt(desc, "Item", "title");
        Check((string)Get(viaColl, "Value") == "Chartboard II", "a collection member reads the same node");

        Put(viaColl, "Value", "Chartboard III");
        object reread = Call(cfg, "Child", "title");
        Check((string)Get(reread, "Value") == "Chartboard III", "and writes through to it");
    }


    // =====================================================================
    // 5. P2Pos, paths, save and load -- late-bound
    // =====================================================================
    static void Demo_PathsAndPersistence (object store)
    {
        Section("5. P2Pos, paths, Save / Load -- by name");

        object width = Call(store, "FieldAt", ".cfg.width");

        object vPos = Get(width, "P2Pos");
        Check(vPos is long, "a hyper crosses as VT_I8 and arrives as a CLR long");
        long pos = (long)vPos;
        Check(pos != 0, "and it is a real position");

        // A host that cannot read a 64-bit integer -- VBScript, 32-bit VBA --
        // uses Path instead, which is why both identities are published.
        Check((string)Get(width, "Path") == ".cfg.width", "Path is the other identity");

        // PATHOF AND PATH ARE ONE SPELLING NOW, and FieldAt takes it back. They
        // were not: PathOf answered the kernel's own grammar, with the root's
        // NAME as the first segment, so a caller that logged one and resolved
        // the other got a silent miss.
        string own = (string)Call(store, "PathOf", pos);
        Check(own == (string)Get(width, "Path"), "PathOf answers the same spelling as Path");
        Check((string)Get(Call(store, "FieldAt", own), "Path") == own, "and FieldAt takes it back");
        Note("PathOf = Path = '{0}'", own);

        // FieldAt takes EITHER identity, and both answer a writable node.
        object byPos = Call(store, "FieldAt", pos);
        Check((string)Get(byPos, "Path") == own, "a P2Pos resolves to the same node");
        Put(byPos, "Value", 1600);
        Check(Convert.ToInt32(Get(width, "Value")) == 1600, "and a write through it lands");

        // Save and reopen, all by name.
        string file = Scratch.Path("MgrCApiTestNet.p2p");
        Scratch.Remove(file);

        Call(store, "Save", file);
        Check((string)Get(store, "Filename") == file, "Filename followed the Save");

        // A SECOND store from the same ProgID -- which is what a script would
        // do, and it needs no reference to the first.
        Type t = Type.GetTypeFromProgID("MsgcoreCom.MsgStore", true);
        object other = Activator.CreateInstance(t);
        Call(other, "Open", file);

        object w2 = Call(other, "FieldAt", ".cfg.width");
        Check(Convert.ToInt32(Get(w2, "Value")) == 1600, "the value came back");

        object tiny2 = Call(other, "FieldAt", ".cfg.tiny");
        Check((string)Get(tiny2, "TypeName") == "UINT08", "and the declared width survived the file");

        // Rename / move / retype, the three structural edits, late-bound.
        object cfg2 = Call(other, "FieldAt", ".cfg");
        Check((bool)Call(cfg2, "RenameChild", "title", "caption"), "RenameChild");
        Check((bool)Call(cfg2, "Exists", "caption"), "and the new name is there");

        // RetypeChild takes a TYPE now, not a value: it used to take a VARIANT
        // ("sixteen hundred" here) because the retype entry points below were
        // one per type. A retype seeds a ZERO by definition, so that argument
        // was always a fiction.
        Check((bool)Call(cfg2, "RetypeChild", "width", MsgDataType.WStr), "RetypeChild");
        object w3 = Call(cfg2, "Child", "width");
        Check((string)Get(w3, "TypeName") == "WSTR16", "and the type changed");

        Call(other, "Close");
        Marshal.ReleaseComObject(other);
        Scratch.Remove(file);
    }


    // =====================================================================
    // 6. What automation does to an HRESULT
    // =====================================================================
    //
    // The C++ harness's version of this section reads EXCEPINFO.scode off the
    // Invoke by hand. The CLR reads it for us -- and then puts it somewhere a
    // careless caller will not look. Both halves are measured here.
    //
    static void Demo_ErrorsThroughInvoke (object store)
    {
        Section("6. What survives the late-bound path -- EXCEPINFO in, success codes out");

        object root = Get(store, "Root");

        // FAILURES SURVIVE, AND SURVIVE WELL. Every one arrives as a
        // COMException carrying the msgc* code and the server's sentence --
        // Err.Number and Err.Description exactly.
        var cases = new List<KeyValuePair<string, KeyValuePair<Action, int>>>();
        cases.Add(new KeyValuePair<string, KeyValuePair<Action, int>>(
            "Child of a name that is not there",
            new KeyValuePair<Action, int>(() => Call(root, "Child", "nosuch"), Hr.msgcNoField)));
        cases.Add(new KeyValuePair<string, KeyValuePair<Action, int>>(
            "Declare with an over-long name",
            new KeyValuePair<Action, int>(() => Call(root, "Declare", new string('z', 64), 1, true), Hr.msgcName)));
        cases.Add(new KeyValuePair<string, KeyValuePair<Action, int>>(
            "Declare with a null value",
            new KeyValuePair<Action, int>(() => Call(root, "Declare", "nothing", null, true), Hr.msgcType)));
        cases.Add(new KeyValuePair<string, KeyValuePair<Action, int>>(
            "List on a node that is not one",
            new KeyValuePair<Action, int>(() => Get(root, "List"), Hr.msgcNotList)));

        foreach (var c in cases)
        {
            var e = Err(c.Value.Key);
            Check(e != null, c.Key + " fails");
            Check(e.Number == c.Value.Value, "with the code the vtable face would have returned");
            Check(e.Description.Length != 0, "and a sentence");
            ShowScriptError(c.Key, e);
        }

        // AND THE WRAPPING, which is what a .NET late-bound client has to know.
        // Reflection wraps whatever the target raised, so the OUTER exception
        // is always the same type with always the same HResult -- 0x80131604,
        // COR_E_TARGETINVOCATION -- for every distinct failure above.
        var one = Err(() => Call(root, "Child", "nosuch"));
        Check(one.OuterType == typeof(TargetInvocationException),
              "the outer exception is TargetInvocationException...");
        Check(one.InnerType == typeof(COMException),
              "...and only the inner one is the COMException that carries the code");
        Note("Inspect the OUTER exception and every Msgcore failure looks identical.");
        Note("PowerShell unwraps this before the script sees it; C# does not.");

        // A SUCCESS CODE OTHER THAN S_OK DOES NOT SURVIVE, and this measures it
        // rather than asserting it. Cursor.Next answers S_FALSE by vtable when
        // it steps off the end; through a late-bound call there is nowhere to
        // put that, so a host must ask EndOfCursor instead.
        object bench = Call(root, "Declare", "bench", 0, true);
        Call(bench, "Declare", "only", 1, true);
        object cur = Get(bench, "Cursor");
        Call(cur, "Seek");
        Check(Err(() => Call(cur, "Next")) == null, "stepping off the end does not fail...");
        Check((bool)Get(cur, "EndOfCursor"), "...so EndOfCursor is the only way to know");
        Note("S_FALSE is normalised away: ask EndOfCursor, never the return value.");
    }


    // =====================================================================
    // main
    // =====================================================================
    [STAThread]
    static int Main ()
    {
        InitConsole();
        Console.WriteLine("=== MgrCApiTestNet - the same store, late-bound by name ===");

        object store = null;
        try
        {
            // THE WHOLE POINT OF THIS FILE IS THAT THESE TWO LINES ARE ALL THE
            // BINDING THERE IS. No interop assembly, no [ComImport], no IID, no
            // type library: a ProgID resolved in the registry at run time --
            // which is exactly what New-Object -ComObject compiles to.
            Type t = Type.GetTypeFromProgID("MsgcoreCom.MsgStore", true);
            store = Activator.CreateInstance(t);
        }
        catch (Exception e)
        {
            return SetupFailure("Type.GetTypeFromProgID(\"MsgcoreCom.MsgStore\")",
                                Marshal.GetHRForException(e));
        }

        Demo_LateBinding            (store);
        Demo_BuildLateBound         (store);
        Demo_DefaultMemberAndForEach(store);
        Demo_NoDetachedNodes        (store);
        Demo_PathsAndPersistence    (store);
        Demo_ErrorsThroughInvoke    (store);

        Call(store, "Close");
        Marshal.ReleaseComObject(store);

        return Verdict();
    }
}
