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
// ListVectTest.cs
//
// Ordered and bulk data: the linked list, the indexed vector, the cursor and
// For Each, from C#. The counterpart of
// ..\ComExamples\ListVectTestCom.
//
// THREE FACTS ABOUT THE CONTAINERS carry over from the C++ COM harness
// unchanged, because they are Msgcore's rather than any caller's:
//
//   Field.Child COULD NOT reach a list or a vect. It resolved through
//   SelectItem, which THROWS on a container node -- a list is not an "item" --
//   so `Child("samples")` failed for a list that was plainly there, and
//   ChildList / ChildVect existed for exactly that gap. Child reaches one now;
//   section 1 pins that instead, and the two typed spellings are a convenience.
//
//   A list is walked in C++ by a raw heap address, which a relocation
//   invalidates, so no such position crosses the ABI. Indexing is offered
//   instead and costs a walk from the head; For Each walks once.
//
//   An element's DECLARED TYPE survives a write. Assigning a double into an
//   Int32 cell converts rather than retypes, because otherwise a sequence's
//   element types would depend on the order a caller happened to write them in.
//
// WHAT IS DIFFERENT HERE is the shape of the indexed accessors. IMsgListCom and
// IMsgVectCom both spell Item as DISPID_VALUE with an index, which a scripting
// host renders as `list(2)` and `list(2) = 9`. C# would render that as an
// indexer -- except that IMsgVectCom's getter and setter are SEVEN VTABLE SLOTS
// APART (the setter was appended with Count, long after the getter), and a C#
// indexer emits its two accessors adjacently. So both interfaces declare Item
// as a pair of ordinary methods, and this harness reads `v.Item(2)` and
// `v.Item(2, 9)`. See the comment on IMsgVectCom in common\MsgcoreComInterop.cs;
// getting this wrong shifts eight members by one slot and fails silently.
//
// Exit codes: 0 SUCCESS  1 SETUP  3 a check failed.

using System;
using System.Collections.Generic;
using System.Text;

using MsgcoreCom;
using Msgc;
using static Msgc.Harness;

static class ListVectTest
{
    // =====================================================================
    // 1. IMsgListCom -- created, mutated, and read back live
    // =====================================================================
    static void Demo_List (Store store)
    {
        Section("1. IMsgListCom -- create a list, fill it, mutate it, re-read it");

        var bag = store.Root.Declare("bag", 0, true);

        // Ordinary data nodes are still not lists, whatever their declared type.
        bag.Declare("anInt", 10, true);
        Check(!bag.Child("anInt").IsList, "a data node is not a list");
        Check(!store.Root.IsList, "and neither is the root");

        // --- create -----------------------------------------------------------
        var l = bag.DeclareList("samples");
        Check(l != null, "DeclareList answered a list");
        Check(l.Count == 0, "which starts empty");

        l.AddTail(11);
        l.AddTail(22);
        l.AddTail("tail");
        l.AddHead(0.5);
        Check(l.Count == 4, "four entries after three tails and a head");

        // --- read by index, typed as stored ------------------------------------
        Check((double)l.Item(0) == 0.5, "the head is the double");
        Check(Convert.ToInt32(l.Item(1)) == 11, "then the first tail");
        Check(Convert.ToInt32(l.Item(2)) == 22, "then the second");
        Check((string)l.Item(3) == "tail", "then the string");

        Check(l.TypeAt(0) == MsgDataType.Double, "element 0 is DOUBLE");
        Check(l.TypeAt(1) == MsgDataType.Int32, "element 1 is INT32");
        Check(l.TypeAt(3) == MsgDataType.WStr, "element 3 is WSTR16");

        // Out of range is a NAMED refusal, not an empty value.
        object scratch;
        int hr = Api.Call(() => l.Item(99), out scratch);
        Check(hr == Hr.msgcRange, "past the end is msgcRange");
        ShowError("List.Item past the end", hr);

        // --- mutate --------------------------------------------------------------
        l.Item(1, 99);
        Check(Convert.ToInt32(l.Item(1)) == 99, "the element write landed");

        // The cell's DECLARED TYPE survives the write: a double into an Int32
        // cell converts, it does not retype the cell. Otherwise a list's element
        // types would depend on the order a caller happened to write them in.
        l.Item(1, 7.9);
        Check(l.TypeAt(1) == MsgDataType.Int32, "7.9 into an Int32 cell leaves it Int32");
        Check(Convert.ToInt32(l.Item(1)) == 8, "...having converted, not truncated");

        Check(l.RemoveAt(1), "RemoveAt reports it removed one");
        Check(l.Count == 3, "count followed");
        Check(!l.RemoveAt(99), "and removing past the end is False, not an error");

        // --- LIVE, not a private copy ---------------------------------------------
        // Everything above went through the handle DeclareList returned. Reach
        // the node again by name and the mutations are there, which is the whole
        // claim.
        var back = bag.ChildList("samples");
        Check(back.Count == 3, "the same list, reached by name");
        Check((double)back.Item(0) == 0.5, "with the same head");
        Check(Convert.ToInt32(back.Item(1)) == 22, "and the same second element");

        // --- Child CAN do this now, which it could not -----------------------------
        var asNode = bag.Child("samples");
        Check(asNode != null, "Child reaches a container node");
        Check(asNode.IsList, "and reports it as a list");
        Check(asNode.Name == "samples", "with its name");

        IMsgVectCom wrongKind;
        hr = Api.Call(() => bag.ChildVect("samples"), out wrongKind);
        Check(hr == Hr.msgcNotVect, "asking for the wrong kind is msgcNotVect");
        ShowError("ChildVect on a node that is a list", hr);

        IMsgListCom absent;
        hr = Api.Call(() => bag.ChildList("nosuch"), out absent);
        Check(hr == Hr.msgcNoField, "and an absent name is msgcNoField -- two different answers");

        // A data node still refuses the list view.
        IMsgListCom notAList;
        hr = Api.Call(() => bag.Child("anInt").List, out notAList);
        Check(hr == Hr.msgcNotList, "the List view of a data node is msgcNotList");

        // --- For Each ---------------------------------------------------------------
        int seen = 0;
        foreach (var v in Api.Each(back._NewEnum())) { seen++; }
        Check(seen == 3, "For Each walked every entry");
        Note("For Each walks a list once; indexing walks from the head every time.");

        back.Truncate();
        Check(back.Count == 0, "Truncate emptied it");
    }


    // =====================================================================
    // 2. IMsgVectCom -- and the Count that used to be missing
    // =====================================================================
    //
    // This interface carried a note for as long as it existed saying it had no
    // Count and no _NewEnum, that the omission was the flat ABI's rather than
    // the COM layer's, and that "the day msgcore_vect_get_count exists, Count
    // and _NewEnum are appended here and the IID does not change". That day
    // came. Both are exercised below, and the IID in
    // common\MsgcoreComInterop.cs is indeed the original one -- so a client
    // built against the older type library still binds. Only a client that
    // calls PAST the old last member needs the current server.
    //
    static void Demo_Vect (Store store)
    {
        Section("2. IMsgVectCom -- create, index, mutate, and the new Count");

        var bag = store.Root.Child("bag");

        var v = bag.DeclareVect("payload", 3, MsgDataType.Int32);
        Check(v != null, "DeclareVect answered a vector");
        Check(v.Count == 3, "with the count that did not use to exist");

        v.Item(0, 5);
        v.Item(1, 6);
        v.Item(2, 7);
        Check(Convert.ToInt32(v.Item(0)) == 5, "element 0");
        Check(Convert.ToInt32(v.Item(2)) == 7, "element 2");
        Check(v.TypeAt(0) == MsgDataType.Int32, "typed by the element prototype");

        int scratch;
        int hr = Api.Call(() => v.TypeAt(99), out scratch);
        Check(hr == Hr.msgcRange, "TypeAt past the end is msgcRange");
        hr = Api.Call(() => v.Item(99, 1));
        Check(hr == Hr.msgcRange, "and so is a write past the end");

        // The element prototype fixes the width, so a Double vect stores doubles.
        var vd = bag.DeclareVect("reals", 2, MsgDataType.Double);
        vd.Item(0, 1.5);
        Check(vd.TypeAt(0) == MsgDataType.Double, "a Double vect holds doubles");
        Check((double)vd.Item(0) == 1.5, "with no rounding on the way in");

        // A zero-length vect is legal: a vect grows.
        var vg = bag.DeclareVect("grow", 0, MsgDataType.Int32);
        Check(vg.Count == 0, "a zero-length vect is legal");

        // --- live, again -----------------------------------------------------------
        var back = bag.ChildVect("payload");
        Check(back.Count == 3, "the same vector, reached by name");
        Check(Convert.ToInt32(back.Item(1)) == 6, "with the element written through the other handle");

        IMsgListCom wrongKind;
        hr = Api.Call(() => bag.ChildList("payload"), out wrongKind);
        Check(hr == Hr.msgcNotList, "and ChildList on it is msgcNotList");

        // --- For Each over the elements ----------------------------------------------
        int seen = 0, sum = 0;
        foreach (var e in Api.Each(back._NewEnum())) { seen++; sum += Convert.ToInt32(e); }
        Check(seen == 3, "For Each walked every element");
        Check(sum == 5 + 6 + 7, "and read the values, not the positions");

        // The aAlloc[32] spill seam -- a vect keeps its first 32 element
        // addresses inline and spills the rest into continuation blocks -- IS
        // reachable now, because a vect of any size can be built. Crossing it
        // changes nothing a caller can observe, which is the point worth
        // recording rather than assuming.
        var big = bag.DeclareVect("wide", 70, MsgDataType.Int32);
        Check(big.Count == 70, "70 elements, so the inline table has spilled");
        big.Item(0,  1000);
        big.Item(31, 1031);          // last inline slot
        big.Item(32, 1032);          // first spilled
        big.Item(69, 1069);
        Check(Convert.ToInt32(big.Item(0))  == 1000, "element 0");
        Check(Convert.ToInt32(big.Item(31)) == 1031, "element 31, the last inline one");
        Check(Convert.ToInt32(big.Item(32)) == 1032, "element 32, the first spilled one");
        Check(Convert.ToInt32(big.Item(69)) == 1069, "element 69");
        Note("The 32-element spill seam is crossed here and is invisible above the heap.");

        // WHAT A VECT ELEMENT ACTUALLY IS, measured rather than assumed. The
        // four predicates exist because an element may be a bare data cell, a
        // named node, a nested list or a nested vect -- and a vect built by
        // DeclareVect from a type prototype holds NAMED NODES, not bare cells:
        // IsField is True and IsData is False, and the name is empty.
        //
        // Which is why Item(i) reads a value off it at all: the element is a
        // node whose value is the element. IsData would be True for a vect some
        // other tool built out of raw P3PmsgData, and this is the predicate to
        // branch on before assuming either.
        Check(!big.IsData(0), "an element of a declared vect is NOT a bare data cell");
        Check(big.IsField(0), "it is a named node -- which is why Item(i) reads a value");
        Check(!big.IsList(0), "not a list");
        Check(!big.IsVect(0), "and not a nested vector");
        Check(big.NameAt(0) == "", "with an empty name: a vect addresses by index, not by name");
        Note("element 0: IsData={0} IsField={1} NameAt='{2}' TypeAt={3}",
             big.IsData(0), big.IsField(0), big.NameAt(0), big.TypeAt(0));
    }


    // =====================================================================
    // 3. What DOES nest -- fields inside fields, arbitrarily deep
    // =====================================================================
    //
    // There is exactly ONE node type in the model, so a tree of them is the
    // general case and a list or a vect is a specialisation. Nesting is
    // therefore not a container feature: it is what a node already does.
    //
    static void Demo_Nesting (Store store)
    {
        Section("3. Nesting -- one node type, arbitrarily deep");

        var root = store.Root.Declare("deep", 0, true);

        // The path grammar INTRODUCES every step with its separator -- '.' for a
        // child, '@' for an attribute -- so the chain starts at ".deep", not
        // "deep", and the root itself is "". That is what makes a path
        // reversible: FieldAt takes back exactly what Path hands out.
        var cur = root;
        var path = new StringBuilder(".deep");
        for (int i = 0; i < 10; ++i)
        {
            string name = "L" + i;
            cur = cur.Declare(name, i * 100, true);
            path.Append('.').Append(name);
        }

        Check(path.ToString() == ".deep.L0.L1.L2.L3.L4.L5.L6.L7.L8.L9", "ten levels");
        Check(cur.Path == path.ToString(), "and the node agrees about its own path");
        Check(Convert.ToInt32(cur.Value) == 900, "the deepest one carries a value");

        // ... and the whole chain is reachable in one call by path, which is
        // what makes a deep tree usable from a script.
        Check(Convert.ToInt32(store.Com.FieldAt(path.ToString()).Value) == 900, "FieldAt reaches the bottom");
        Check(Convert.ToInt32(store.Com.FieldAt(".deep.L0.L1.L2").Value) == 200, "and any level between");

        // Every level carries a value AND children at the same time. A node is
        // not either a leaf or an interior node; it is both whenever it wants
        // to be.
        Check(Convert.ToInt32(store.Com.FieldAt(".deep.L0").Value) == 0, "an interior node has a value");
        Check(store.Com.FieldAt(".deep.L0").Count == 1, "and children");

        // A branch, so the tree is not merely a chain.
        var l0 = store.Com.FieldAt(".deep.L0");
        l0.Declare("sibling", "beside L1", true);
        Check(l0.Count == 2, "two children now");
        Check((string)store.Com.FieldAt(".deep.L0.sibling").Value == "beside L1", "and the branch is addressable");

        // Attributes hang off interior nodes as happily as off leaves -- the
        // second collection is a property of a NODE, not of a leaf.
        var a = l0.Attributes(true);
        a.Declare("depth", 0, true);
        Check(a.Count == 1, "one attribute on an interior node");
        Check(l0.Count == 2, "and the descendants are unchanged");

        Note("10 levels, one branch, attributes on an interior node: {0} = {1}",
             path, cur.Value);
    }


    // =====================================================================
    // 4. IMsgCursorCom -- generic traversal
    // =====================================================================
    //
    // ONE THING IS DELIBERATELY NOT A TRANSLATION, and it is the most important
    // paragraph in this file. The flat library's IsEoCursor is
    //
    //      nItems <= 0 || m_nItem >= nItems - 1
    //
    // -- TRUE ON THE LAST ELEMENT, not after it. So the loop everyone writes,
    // `for (c.Seek(); !c.IsEoCursor(); ++c)`, silently visits every element but
    // the last, with no error and no short read: a three-child node enumerates
    // as two. IMsgCursorCom.EndOfCursor means what its help string says instead
    // -- "the cursor has walked off the end" -- so the natural loop is correct
    // here. That is a DIFFERENT predicate from the flat one, on purpose.
    //
    static void Demo_Cursor (Store store)
    {
        Section("4. IMsgCursorCom -- generic traversal, and the EndOfCursor trap");

        var bench = store.Root.Declare("bench", 0, true);

        var names = new[] { "alpha", "bravo", "charlie", "delta", "echo" };
        for (int i = 0; i < names.Length; ++i) bench.Declare(names[i], (i + 1) * 10, true);
        Check(bench.Count == 5, "five children");

        var c = bench.Cursor;
        Check(c.Count == 5, "and the cursor spans all five");

        // THE LOOP. If EndOfCursor had been a transliteration of IsEoCursor,
        // this would count 4 and sum 100 -- and would look right.
        int visited = 0, sum = 0;
        var order = new StringBuilder();
        for (c.Seek(); !c.EndOfCursor; c.Next())
        {
            order.Append(c.Name).Append(' ');
            sum += Convert.ToInt32(c.Field.Value);
            ++visited;
        }
        Check(visited == 5, "the natural loop visits every element");
        Check(sum == 150, "including the last one");
        Check(order.ToString() == "alpha bravo charlie delta echo ", "in declaration order");
        Note("forward : {0} elements summing to {1}", visited, sum);
        Note("order   : {0}", order);

        // Positioning. Index is a POSITION, never a handle -- the set is live.
        Check(c.GotoIndex(2), "GotoIndex found element 2");
        Check(c.Index == 2, "and the index says so");
        Check(c.Name == "charlie", "which is charlie");
        Check(c.IsItem, "a plain item");
        Check(!c.IsList, "not a list");
        Check(!c.IsVect, "not a vector");

        Check(c.GotoName("echo"), "GotoName found echo");
        Check(c.Index == 4, "at index 4");
        Check(Convert.ToInt32(c.Field.Value) == 50, "with its value");

        Check(!c.GotoName("nobody"), "a name that is not there is False");
        Check(c.Index == 4, "and the position did not move");
        Check(!c.GotoIndex(99), "an index past the end is False");
        Check(c.Index == 4, "and the position still did not move");

        // Walking off the end is a state, and Index reports it as -1 rather
        // than as a plausible position.
        c.Next();
        Check(c.EndOfCursor, "after the last element the cursor is at the end");
        Check(c.Index == -1, "and Index is -1, not a plausible position");

        c.Seek();
        Check(c.StartOfCursor, "Seek rewinds to the start");
        Check(!c.EndOfCursor, "which is not the end");
        Check(c.Index == 0, "at index 0");

        // An element reached through a cursor is handed back LIVE, so a walk can
        // also be an edit. That used to be true only over a CHILD collection:
        // over an attribute collection there was no live accessor to reach one
        // with, and IsLive was how a caller found out which of the two they had.
        // Both are live now, and IsLive is gone with the distinction.
        Check(c.GotoName("bravo"), "back to bravo");
        var bravo = c.Field;
        Check(bravo != null, "an element of a cursor is a node");
        Check(bravo.Path == ".bench.bravo", "and knows its path");
        bravo.Value = 999;
        Check(Convert.ToInt32(bench.Child("bravo").Value) == 999, "so a walk can be an edit");

        // A CURSOR SURVIVES A MUTATION, which the flat P3PmsgCurs does not: it
        // is rebuilt from the owner and re-seeked on every call, so a heap
        // relocation underneath it is invisible. Add 40 children and the
        // position still means what it meant.
        Check(c.GotoIndex(1), "park on index 1");
        for (int i = 0; i < 40; ++i) bench.Declare(string.Format("filler{0:00}", i), i, true);
        Check(c.Count == 45, "the cursor sees the new children");
        Check(c.Index == 1, "and is still on index 1");
        Check(c.Name == "bravo", "which is still bravo -- the heap moved, the position did not");
    }


    // =====================================================================
    // 5. _NewEnum -- For Each, and why it is a snapshot
    // =====================================================================
    //
    // `For Each child In field` is what a scripting host compiles a walk into,
    // and its contract is ONE PASS OVER A FIXED SET. A live position cannot
    // promise that across a mutation -- delete an element and everything after
    // it shifts down into the gap -- so _NewEnum takes the elements up front.
    //
    // The difference is not academic: deleting as you walk is the single most
    // common thing a For Each body does.
    //
    // In C# the walk is Api.Each, which is the same IEnumVARIANT the host would
    // have driven. What it yields for a FIELD collection is an IMsgFieldCom,
    // not a value -- the enumerator hands out the elements, and an element of a
    // node's child collection is a node.
    //
    static void Demo_ForEach (Store store)
    {
        Section("5. _NewEnum -- a snapshot, so a handler may delete as it walks");

        var bin = store.Root.Declare("inbox", 0, true);
        for (int i = 0; i < 8; ++i) bin.Declare("msg" + i, i, true);
        Check(bin.Count == 8, "eight messages");

        // A plain read pass sees every element, in declaration order.
        int seen = 0, sum = 0;
        foreach (var o in Api.Each(bin._NewEnum()))
        {
            var f = (IMsgFieldCom)o;
            ++seen;
            sum += Convert.ToInt32(f.Value);
        }
        Check(seen == 8, "the read pass saw all eight");
        Check(sum == 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7, "and summed their values");

        // The elements are LIVE, so For Each is also an edit pass.
        foreach (var o in Api.Each(bin._NewEnum()))
        {
            var f = (IMsgFieldCom)o;
            f.Value = Convert.ToInt32(f.Value) * 2;
        }
        Check(Convert.ToInt32(bin.Child("msg7").Value) == 14, "the edit pass landed");

        // AND THE ONE THAT MATTERS: delete the even ones while walking. Against
        // a live position this either skips elements or walks off the end;
        // against a snapshot it does exactly what it reads as.
        int visitedDuringDelete = 0;
        foreach (var o in Api.Each(bin._NewEnum()))
        {
            var f = (IMsgFieldCom)o;
            ++visitedDuringDelete;
            int value = Convert.ToInt32(f.Value);
            if ((value / 2) % 2 == 0) store.Root.Child("inbox").Delete(f.Name);
        }
        Check(visitedDuringDelete == 8, "every element was visited despite the deletes");
        Check(bin.Count == 4, "and four survived");
        Check(!bin.Exists("msg0"), "msg0 went");
        Check(bin.Exists("msg1"), "msg1 stayed");
        Check(!bin.Exists("msg2"), "msg2 went");
        Check(bin.Exists("msg7"), "msg7 stayed");

        Note("visited {0}, {1} survived: msg1 msg3 msg5 msg7", visitedDuringDelete, bin.Count);

        // Cursor.Delete is the other way to do it, and is a LIVE position: it
        // removes the current element and the index does NOT advance, because
        // everything after it has shifted down into that slot.
        var c = bin.Cursor;
        c.Seek();
        Check(c.Count == 4, "four before");
        string first = c.Name;
        c.Delete();
        Check(c.Count == 3, "three after");
        Check(!bin.Exists(first), "and the first one is gone");
        Check(c.Index == 0, "the index is still 0: the set moved, not the cursor");

        // Walk what is left, then an empty collection -- the degenerate case
        // the count-based loop has to get right too.
        int left = 0;
        for (c.Seek(); !c.EndOfCursor; c.Next()) ++left;
        Check(left == 3, "three left");

        bin.Truncate();
        Check(bin.Count == 0, "and none after Truncate");
        var empty = bin.Cursor;
        Check(empty.Count == 0, "an empty cursor spans nothing");
        Check(empty.EndOfCursor, "and is at the end immediately, with no element read");

        int never = 0;
        for (empty.Seek(); !empty.EndOfCursor; empty.Next()) ++never;
        Check(never == 0, "so the loop body never runs");

        int neverEnum = 0;
        foreach (var o in Api.Each(bin._NewEnum())) { neverEnum++; }
        Check(neverEnum == 0, "and neither does For Each");
    }


    // =====================================================================
    // main
    // =====================================================================
    [STAThread]
    static int Main ()
    {
        InitConsole();
        Console.WriteLine("=== ListVectTestNet - ordered data from C#, and where it stops ===");

        int hr;
        using (var store = Store.Create(out hr))
        {
            if (store == null) return SetupFailure("new MsgcoreCom.MsgStore", hr);

            Demo_List    (store);
            Demo_Vect    (store);
            Demo_Nesting (store);
            Demo_Cursor  (store);
            Demo_ForEach (store);
        }

        return Verdict();
    }
}
