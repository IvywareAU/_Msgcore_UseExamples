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
// RecursTimeTest.cs
//
// Walking a whole subtree, and the cell that is known to be a time. The
// counterpart of ..\ComExamples\RecursTimeTestCom.
//
// TWO SUBJECTS, and one of them reads better in C# than anywhere else in MSCS:
//
//   THE WALKER. Sections 1 to 3 flatten the tree with a client-side work list,
//   which is what this tier had before Msgcore_c.h grew msgcore_recurs_*.
//   Section 6 uses the real IMsgRecursCom, which holds a chain of live cursors
//   and nothing else -- and the difference is the whole point: the emulation
//   materialises every path it has not yet visited, which is exactly the cost a
//   walker exists to avoid.
//
//   THE TIMESTAMP. Msgcore stores it as the "$TStamp$" attribute in whole
//   seconds since the epoch; the COM layer converts to an automation DATE; and
//   the CLR converts THAT to a System.DateTime. So three conversions stand
//   between the store and section 4, and what arrives is a DateTime a caller can
//   compare and format with no arithmetic at all. The C++ harness next door
//   spends six lines on SystemTimeToVariantTime for each of these checks.
//
// Exit codes: 0 SUCCESS  1 SETUP  3 a check failed.

using System;
using System.Collections.Generic;
using System.Text;

using MsgcoreCom;
using Msgc;
using static Msgc.Harness;

static class RecursTimeTest
{
    /// <summary>DATE 0.0 -- the automation epoch, and what "unset" reads as.</summary>
    static readonly DateTime Unset = new DateTime(1899, 12, 30);

    /// <summary>One stop on a walk -- what IMsgRecursCom exposes at each iteration.</summary>
    sealed class Stop
    {
        public string Path;
        public string Name;
        public int    Depth;
        public int    Children;
    }

    // A tree worth walking: three levels, branching, with values at every level
    // so that "interior nodes carry data too" is exercised rather than assumed.
    static void BuildTree (Store store)
    {
        var site = store.Root.Declare("site", "melbourne", true);

        var floors = new[] { "ground", "first", "second" };
        for (int f = 0; f < floors.Length; ++f)
        {
            var floor = site.Declare(floors[f], f, true);
            for (int r = 0; r < 2; ++r)
            {
                var room = floor.Declare("room" + r, f * 10 + r, true);
                room.Declare("temp", 20.0 + f + r, true);
                room.Declare("name", "a room", true);
            }
        }
    }


    // =====================================================================
    // 1. One flat loop over a whole subtree
    // =====================================================================
    //
    // A walker exists so that a caller does not have to write a recursive
    // function: it turns a tree into a sequence. The same shape is a dozen
    // lines over IMsgCursorCom -- and because EndOfCursor here means "walked off
    // the end" (and NOT the flat IsEoCursor's "on the last element"), the
    // obvious loop is the correct one.
    //
    static List<Stop> Demo_FlatWalk (Store store)
    {
        Section("1. One flat loop over a whole subtree");

        var stops = new List<Stop>();

        // An explicit work stack, so the walk is a LOOP and not recursion --
        // which is what makes it portable to a host with no call-stack depth to
        // spend.
        var work = new Stack<KeyValuePair<string,int>>();
        work.Push(new KeyValuePair<string,int>(".site", 0));

        while (work.Count != 0)
        {
            var item  = work.Pop();
            string path = item.Key;
            int    depth = item.Value;

            var f = store.Com.FieldAt(path);
            if (f == null) continue;

            stops.Add(new Stop { Path = path, Name = f.Name, Depth = depth, Children = f.Count });

            // Children pushed in reverse so they come off in declaration order.
            var kids = new List<string>();
            var c = f.Cursor;
            for (c.Seek(); !c.EndOfCursor; c.Next()) kids.Add(c.Name);
            for (int i = kids.Count; i > 0; --i)
                work.Push(new KeyValuePair<string,int>(path + "." + kids[i-1], depth + 1));
        }

        // 1 site + 3 floors + 6 rooms + 12 leaves = 22 stops.
        Check(stops.Count == 22, "22 stops for a 1+3+6+12 tree");
        Check(stops[0].Path == ".site", "starting at the root of the subtree");
        Check(stops[0].Depth == 0, "at depth 0");
        Check(stops[0].Children == 3, "with three floors");

        int maxDepth = 0, leaves = 0;
        foreach (var s in stops)
        {
            if (s.Depth > maxDepth) maxDepth = s.Depth;
            if (s.Children == 0)    ++leaves;
        }
        Check(maxDepth == 3, "three levels below the root");
        Check(leaves == 12, "twelve leaves");
        Note("visited {0} nodes, max depth {1}, {2} leaves", stops.Count, maxDepth, leaves);

        // Declaration order is preserved all the way down, which is what makes
        // a walk reproducible -- and therefore what makes it diffable.
        Check(stops[1].Path == ".site.ground", "declaration order at level 1");
        Check(stops[2].Path == ".site.ground.room0", "and at level 2");
        Check(stops[3].Path == ".site.ground.room0.temp", "and at level 3");

        return stops;
    }


    // =====================================================================
    // 2. Driving the descent by hand -- push, pop and break
    // =====================================================================
    static void Demo_DrivenDescent (Store store)
    {
        Section("2. Push / Pop / Break -- steering the descent");

        // PRUNE: descend into everything except one subtree. This is the case a
        // walker's "do not push" is for, and it is the common one -- a config
        // reader that skips a cache branch, a backup that skips a scratch node.
        var work = new Stack<string>();
        work.Push(".site");
        int visited = 0, pruned = 0;

        while (work.Count != 0)
        {
            string path = work.Pop();
            ++visited;

            var f = store.Com.FieldAt(path);
            if (f == null) continue;
            if (f.Name == "first") { ++pruned; continue; }      // do not descend

            var c = f.Cursor;
            for (c.Seek(); !c.EndOfCursor; c.Next()) work.Push(path + "." + c.Name);
        }

        // 22 total, minus the 6 BELOW "first" that were never pushed. "first"
        // itself is still visited -- the prune decides whether to DESCEND, which
        // is the distinction a walker's Push/Pop is about.
        Check(pruned == 1, "one node pruned");
        Check(visited == 22 - 6, "and its six descendants never visited");
        Note("pruned 'first': {0} nodes visited instead of 22", visited);

        // BREAK: stop at the first match. The point of a walker is that this
        // costs what it costs and not a full traversal.
        var work2 = new Stack<string>();
        work2.Push(".site");
        int beforeHit = 0;
        string found = null;

        while (work2.Count != 0)
        {
            string path = work2.Pop();
            ++beforeHit;

            var f = store.Com.FieldAt(path);
            if (f == null) continue;

            if (f.Name == "temp" && Convert.ToDouble(f.Value) >= 22.0) { found = path; break; }

            var kids = new List<string>();
            var c = f.Cursor;
            for (c.Seek(); !c.EndOfCursor; c.Next()) kids.Add(c.Name);
            for (int i = kids.Count; i > 0; --i) work2.Push(path + "." + kids[i-1]);
        }

        Check(found != null, "the break found something");
        Check(found == ".site.first.room1.temp", "the first temp at or above 22.0");
        Check(beforeHit < 22, "after fewer than 22 stops");
        Note("break on first temp >= 22.0: '{0}' after {1} stops", found, beforeHit);

        // POP past a whole level: collect one depth only, which is what a "list
        // the floors" query is, and needs no descent at all.
        var order = new StringBuilder();
        var sc = store.Com.FieldAt(".site").Cursor;
        for (sc.Seek(); !sc.EndOfCursor; sc.Next()) order.Append(sc.Name).Append(' ');
        Check(order.ToString() == "ground first second ", "one level, in declaration order");
    }


    // =====================================================================
    // 3. What the walk can read at each stop
    // =====================================================================
    static void Demo_ReadAtEachStop (Store store, List<Stop> stops)
    {
        Section("3. Reading the tree through the walk");

        int typed = 0, doubles = 0, strings = 0, ints = 0;

        foreach (var s in stops)
        {
            var f = store.Com.FieldAt(s.Path);
            Check(f != null, "every recorded path still resolves");
            if (f == null) continue;

            // Every stop answers the same four questions, whatever it is --
            // which is the uniformity the model claims, and this is the test of
            // it.
            Check(f.Path == s.Path, "with the path it was reached by");
            Check(f.Name == s.Name, "the name recorded on the way past");
            Check(f.Count == s.Children, "and the same child count");
            Check(f.TypeName.Length != 0, "every node has a type name");

            ++typed;
            int t = f.DataType;
            if      (t == MsgDataType.Double) ++doubles;
            else if (t == MsgDataType.WStr)   ++strings;
            else if (t == MsgDataType.Int32)  ++ints;
        }

        Check(typed == 22, "22 nodes classified");
        Check(doubles == 6, "one temp per room");
        Check(strings == 1 + 6, "\"site\" plus one name per room");
        Check(ints == 3 + 6, "the floors and the rooms");
        Note("22 stops: {0} double, {1} string, {2} int", doubles, strings, ints);

        // A walk that WRITES, which is the other half of what a walker is for.
        foreach (var s in stops)
        {
            var f = store.Com.FieldAt(s.Path);
            if (f != null && f.DataType == MsgDataType.Double)
                f.Value = Convert.ToDouble(f.Value) + 100.0;
        }
        Check(Convert.ToDouble(store.Com.FieldAt(".site.ground.room0.temp").Value) == 120.0,
              "the edit pass reached the first temp");
        Check(Convert.ToDouble(store.Com.FieldAt(".site.second.room1.temp").Value) == 123.0,
              "and the last one");
    }


    // =====================================================================
    // 4. A node's timestamp -- P3PmsgTime, as a DateTime
    // =====================================================================
    //
    // TWO CONSEQUENCES ARE WORTH PINNING, and both are exercised below: the
    // resolution is ONE SECOND (what is stored is an integer count, so a
    // DateTime carrying fractions of a second does not survive), and stamping a
    // node MAKES IT ATTRIBUTED -- the timestamp is not a separate slot, it is an
    // attribute.
    //
    static void Demo_Timestamp (Store store)
    {
        Section("4. Timestamp -- the TIME64 cell, seen as a DateTime");

        var room = store.Com.FieldAt(".site.ground.room0");

        // Unset reads as DATE 0.0, which is 30 December 1899 -- the automation
        // epoch, and the value a host renders as "no date".
        Check(room.Timestamp == Unset, "an unstamped node reads as the automation epoch");
        Check(!room.IsAttributed, "and carries no attributes");

        // Touch writes "now". Both sides are UTC: the COM layer converts the
        // epoch seconds through SystemTimeToVariantTime without a local
        // adjustment, so this compares against UtcNow and not Now.
        DateTime before = DateTime.UtcNow;
        room.Touch();
        DateTime stamped = room.Timestamp;
        Check(stamped != Unset, "Touch wrote something");

        // Within a minute of now, which is all a clock comparison can honestly
        // claim across a call.
        Check(stamped > before.AddMinutes(-1), "which is not in the past");
        Check(stamped < before.AddMinutes(1), "and not in the future");
        Note("Touch()     : {0:yyyy-MM-dd HH:mm:ss}", stamped);

        // AND IT IS AN ATTRIBUTE. Stamping a node gives it an attribute
        // collection it did not have, which is visible in IsAttributed and in
        // the collection itself -- not a hidden slot.
        Check(room.IsAttributed, "stamping made the node attributed");
        var a = room.Attributes(false);
        Check(a.Count >= 1, "the collection exists");
        Check(a.Exists("$TStamp$"), "and holds $TStamp$");
        Note("stored as   : the '$TStamp$' attribute, {0} attribute(s) on the node", a.Count);

        // An explicit date round-trips to the SECOND. Sub-second precision does
        // not survive, because what is stored is an integer count of seconds --
        // so the fractional part below is deliberately dropped by the store, not
        // by this harness.
        var fixedTime = new DateTime(2026, 8, 11, 14, 30, 45);
        room.Timestamp = fixedTime;
        DateTime read = room.Timestamp;
        Check(read.Year == 2026 && read.Month == 8 && read.Day == 11, "the date survived");
        Check(read.Hour == 14 && read.Minute == 30 && read.Second == 45, "and the time, to the second");

        room.Timestamp = fixedTime.AddMilliseconds(400);
        Check(room.Timestamp.Millisecond == 0, "sub-second precision does not survive");

        // The rounding is deliberate and it matters: truncating instead would
        // lose a second about half the time, and a timestamp that walks
        // BACKWARDS on every read-modify-write cycle is a genuinely confusing
        // bug to chase.
        room.Timestamp = fixedTime;
        for (int i = 0; i < 5; ++i)
        {
            DateTime d = room.Timestamp;
            room.Timestamp = d;
            Check(room.Timestamp == d, "a read-modify-write cycle is stable, not drifting");
        }

        // Zero means unset in both directions.
        room.Timestamp = Unset;
        Check(room.Timestamp == Unset, "the epoch means unset in both directions");

        room.Timestamp = fixedTime;
    }


    // =====================================================================
    // 5. A timestamp through Save / Load
    // =====================================================================
    static void Demo_TimestampPersists (Store store)
    {
        Section("5. A timestamp through Save / Load");

        DateTime before = store.Com.FieldAt(".site.ground.room0").Timestamp;
        Check(before != Unset, "there is a timestamp to lose");

        // Stamp a few more nodes, so what survives is a pattern and not one
        // value.
        store.Com.FieldAt(".site.first").Touch();
        store.Com.FieldAt(".site.second.room1.temp").Touch();
        DateTime first = store.Com.FieldAt(".site.first").Timestamp;
        DateTime temp  = store.Com.FieldAt(".site.second.room1.temp").Timestamp;

        string file = Scratch.Path("RecursTimeTestNet.p2p");
        Scratch.Remove(file);
        store.Com.Save(file);

        int hr;
        using (var loaded = Store.Create(out hr))
        {
            Check(loaded != null, "a second store to load into");
            loaded.Com.Open(file);

            Check(loaded.Com.FieldAt(".site.ground.room0").Timestamp == before, "the first stamp survived");
            Check(loaded.Com.FieldAt(".site.first").Timestamp == first, "the second one too");
            Check(loaded.Com.FieldAt(".site.second.room1.temp").Timestamp == temp, "and the third");

            // A node that was never stamped is still unstamped, so the
            // attribute is genuinely per-node and not a default the format
            // invents on load.
            Check(loaded.Com.FieldAt(".site.ground.room1").Timestamp == Unset, "an unstamped node stayed unstamped");
            Check(!loaded.Com.FieldAt(".site.ground.room1").IsAttributed, "and unattributed");

            // ... and the walk still finds the same 22 nodes on the far side,
            // so the whole subtree survived, not just the values checked by
            // name.
            int nStops = 0;
            var work = new Stack<string>();
            work.Push(".site");
            while (work.Count != 0)
            {
                string path = work.Pop();
                ++nStops;
                var f = loaded.Com.FieldAt(path);
                if (f == null) continue;
                var c = f.Cursor;
                for (c.Seek(); !c.EndOfCursor; c.Next()) work.Push(path + "." + c.Name);
            }
            Check(nStops == 22, "22 nodes and 3 timestamps survived the round trip");
        }

        Scratch.Remove(file);
    }


    // =====================================================================
    // 6. The REAL walker -- IMsgRecursCom, not an emulation
    // =====================================================================
    //
    // Sections 1 and 2 steer a descent with a client-side work list, which is
    // what this tier had when they were written. The real walker holds a chain
    // of cursors, one per pushed level, and nothing else -- so it is the one
    // object in this server that is NOT re-resolved per call. It cannot be: a
    // cursor IS a live position, and snapshotting it would defeat the purpose.
    // It answers msgcStale if the tree is mutated under it. Finish the walk,
    // then mutate.
    //
    static void Demo_RealWalker (Store store)
    {
        Section("6. IMsgRecursCom -- the walker itself, with real pruning");

        // A small tree of known shape, so the visit count is arithmetic and not
        // a guess: keep(2 kids) + skip(2 kids) + leaf = 3 top, 4 below.
        var w = store.Root.Declare("walk", 0, true);
        var keep = w.Declare("keep", 1, true);
        keep.Declare("k1", 11, true);
        keep.Declare("k2", 12, true);
        var skip = w.Declare("skip", 2, true);
        skip.Declare("s1", 21, true);
        skip.Declare("s2", 22, true);
        w.Declare("leaf", 3, true);

        // --- descend into everything -----------------------------------------
        {
            var r = store.Com.FieldAt(".walk").Walker;
            Check(r != null, "Field.Walker answered an IMsgRecursCom");

            int visited = 0, guard = 0;
            while (!r.AtEnd && ++guard < 100)
            {
                ++visited;
                if (r.IsField) r.Push();          // descend everywhere
                r.MoveNext();
            }
            Check(guard < 100, "the walk terminated on its own");
            Check(visited == 7, "and saw 3 top-level plus 4 below");
            Note("full descent visited {0} nodes", visited);
        }

        // --- prune: the same walk, not descending into "skip" -----------------
        // The prune is the ABSENCE of a Push. Nothing is skipped over: "skip"
        // itself is still visited, only its children are not -- which is the
        // distinction Push/Pop is about, and the one a For Each cannot express.
        {
            var r = store.Com.FieldAt(".walk").Walker;

            int visited = 0, guard = 0;
            bool sawSkip = false, sawS1 = false;
            while (!r.AtEnd && ++guard < 100)
            {
                string nm = r.Name;
                if (nm == "skip") sawSkip = true;
                if (nm == "s1")   sawS1   = true;
                ++visited;
                if (r.IsField && nm != "skip") r.Push();
                r.MoveNext();
            }
            Check(sawSkip, "'skip' was visited");
            Check(!sawS1, "but not descended into");
            Check(visited == 5, "so 5 nodes instead of 7");
            Note("pruned 'skip': {0} nodes instead of 7", visited);
        }

        // --- the current element is an ordinary, addressable node -------------
        {
            var r = store.Com.FieldAt(".walk").Walker;
            Check(!r.AtEnd, "the walk has somewhere to start");

            var cur = r.Field;
            Check(cur != null, "the current element is a node");
            Check(cur.Name == r.Name, "with the walker's current name");

            // The walker reports its own DEPTH and PATH now, which it could not
            // before. Path is assembled as the walk moves, so unlike everything
            // else on the walker it stays true after the walk has gone past.
            Check(r.Depth == 0, "a fresh walker is at the outermost level");
            Check(r.Path == cur.Path, "and its path is the current stop's");
        }

        // --- a container element is classified, and reachable as one ----------
        {
            var c = store.Root.Declare("wc", 0, true);
            var cl = c.DeclareList("clist");
            cl.AddTail(7);

            var r = store.Com.FieldAt(".wc").Walker;
            Check(!r.AtEnd, "the container is the first stop");
            Check(r.IsList, "and the walker says it is a list");
            Check(!r.IsField, "not a plain node");

            var got = r.List;
            Check(got.Count == 1, "reachable as a list");
            Check(Convert.ToInt32(got.Item(0)) == 7, "with its element");

            IMsgVectCom wrong;
            int hr = Api.Call(() => r.Vector, out wrong);
            Check(hr == Hr.msgcNotVect, "and asking for the other kind is msgcNotVect");
        }

        // --- a walker holds LIVE cursors, and what that costs ------------------
        //
        // This block used to assert that a mutation ANYWHERE invalidated the
        // walker and that the next Push answered msgcStale. That is no longer
        // what happens, and the difference is the whole reason the layer below
        // this one exists: the walker's cursors are re-derived positions rather
        // than raw addresses, so a mutation on ANOTHER branch does not disturb
        // them even when it relocates the heap.
        //
        // A mutation INSIDE the subtree being walked is a different matter and
        // is not checked here, because there is nothing deterministic to check:
        // a walker is not a snapshot -- being one is the thing it exists not to
        // be -- so what a caller gets is whatever the tree now is. The rule is
        // the same as it always was, and it is a rule rather than an error code:
        // finish the walk, then mutate.
        {
            var r = store.Com.FieldAt(".walk").Walker;
            Check(!r.AtEnd, "a fresh walker");
            string before = r.Name;

            // Elsewhere in the store, and big enough to move the heap under it.
            store.Root.Declare("disturb", 1, true);
            var bulk = store.Root.Declare("disturbBulk", 0, true);
            for (int i = 0; i < 50; ++i)
                bulk.Declare("d" + i, "padding, to force the heap to grow", true);

            Check(r.Name == before, "the walker is still where it was");
            int depth;
            Check(Api.Call(() => r.Push(), out depth) == Hr.S_OK, "and still usable");

            Check(store.Root.Delete("disturb") && store.Root.Delete("disturbBulk"), "tidy up");
            Note("A walker survives a mutation on another branch. Inside the");
            Note("subtree it is walking, finish first -- it is not a snapshot.");
        }
    }


    // =====================================================================
    // 7. The value stack -- save a node's name and value, put them back
    // =====================================================================
    //
    // THIS USED TO BE AN OBJECT. IMsgStackCom wrapped the core's MsgStck with a
    // lifetime of its own, and two of its behaviours had to be NORMALISED by the
    // COM layer rather than passed through, because both were traps:
    //
    //   MsgStck::IsEmpty answers FALSE for a stack connected to nothing -- "not
    //   empty" for a stack holding nothing -- so a drain loop would never end.
    //
    //   MsgStck::Push and ::Pop dereference their field with no null check of
    //   their own, unlike Drop, Rename and r_item which all guard, so pushing an
    //   unconnected stack is a null dereference inside the core.
    //
    // Both disappear when the object does. What it wrapped is ONE saved (name,
    // value) pair living INSIDE a node, so it is four members of the node: there
    // is no unconnected stack to push, and Pop answers whether it restored
    // anything instead of merely succeeding.
    //
    // A THIRD TRAP WAS FOUND BY WRITING THIS. MsgStck::Push read a physical heap
    // pointer, then allocated the stack item, then wrote through the pointer it
    // had already read -- and an allocation that grows the heap RELOCATES the
    // base image. Reliable once a store was full enough for the push to trigger
    // a growth, which is why it survived every previous test: they all pushed
    // into a nearly-empty store. Fixed in Msgcore; this section pushes into a
    // store with a few hundred nodes in it for that reason.
    //
    static void Demo_Stack (Store store)
    {
        Section("7. The value stack -- four members of a node, not an object");

        var s = store.Root.Declare("stk", "original", true);
        s.Declare("a", 1, true);

        var node = store.Com.FieldAt(".stk");
        Check(node != null, "the node the stack lives in");
        Check(node.Name == "stk", "is the one it was reached by");
        Check(!node.IsStacked, "with nothing stacked yet");

        // Push, overwrite, put it back.
        node.PushValue();
        Check(node.IsStacked, "PushValue saved the pair");
        node.Value = "speculative";
        Check((string)node.Value == "speculative", "and the node can be overwritten");
        Check(node.PopValue(), "PopValue restored it, and says so");
        Check((string)node.Value == "original", "with the old value back");
        Check(!node.IsStacked, "and nothing stacked");

        // The children are untouched: it is the node's own name and value that
        // are saved, not its subtree.
        Check(node.Count == 1, "the subtree is untouched");
        Check(Convert.ToInt32(node.Child("a").Value) == 1, "including its value");

        // POP IS THE LOOP CONDITION. It answers False for "there was nothing
        // stacked", which is what makes a drain loop terminate -- the old
        // object's Pop was a silent no-op that still returned success.
        int drained = 0;
        while (node.PopValue()) ++drained;
        Check(drained == 0, "a drain loop over an empty stack does nothing, and ENDS");

        // The saved pair lives in the STORE, so another reference sees it.
        var again = store.Root.Child("stk");
        node.PushValue();
        Check(again.IsStacked, "a second reference sees the saved pair");
        Check(again.DropValue(), "and can drop it");
        Check(!node.IsStacked, "which the first reference sees");
        Check((string)node.Value == "original", "dropped, not restored");

        Note("PushValue/PopValue/DropValue/IsStacked, all on the node. Pop's");
        Note("ANSWER drives a drain loop -- never merely whether the call worked.");
    }


    // =====================================================================
    // main
    // =====================================================================
    [STAThread]
    static int Main ()
    {
        InitConsole();
        Console.WriteLine("=== RecursTimeTestNet - walking a subtree, and the time cell ===");

        int hr;
        using (var store = Store.Create(out hr))
        {
            if (store == null) return SetupFailure("new MsgcoreCom.MsgStore", hr);

            BuildTree(store);

            var stops = Demo_FlatWalk(store);
            Demo_DrivenDescent     (store);
            Demo_ReadAtEachStop    (store, stops);
            Demo_Timestamp         (store);
            Demo_TimestampPersists (store);
            Demo_RealWalker        (store);
            Demo_Stack             (store);
        }

        return Verdict();
    }
}
