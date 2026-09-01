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
// MgrPersistTest.cs
//
// A store as a DOCUMENT -- one heap owning one tree, saved, loaded, addressed
// by position, watched for changes, and paged. The counterpart of
// ..\ComExamples\MgrPersistTestCom.
//
// THIS FILE HOLDS THE ONE MEASUREMENT THIS TREE MAKES THAT NO OTHER TREE CAN.
//
// Both sinks in MsgcoreCom -- the queued OnChange and the synchronous paging
// callbacks -- are IDispatch objects, and in the C++ trees both are apartment
// bound. MsgcoreCom parks an event sink in the Global Interface Table and
// re-fetches it on its own MTA dispatch thread, and what comes back for a C++
// sink is a PROXY, so the call marshals into the client's STA and every C++
// harness sees `tid=` unchanged from end to end.
//
// A MANAGED SINK IS AGILE. The CLR's CCW aggregates the free-threaded
// marshaler, so the same GIT re-fetch hands the dispatch thread the IDENTICAL
// pointer, and OnChange runs ON THE DISPATCH THREAD -- section 4 measures it.
// The consequence is not academic: a C# handler is on a thread the client never
// created, concurrently with main, so everything it touches must be safe for
// that. The C++ harness next door can be written as if it were single-threaded,
// and is.
//
// The pump stays anyway (Gate.Wait pumps), because depending on the CCW staying
// agile is how you get a client that hangs on a customer's machine and not on
// yours.
//
// Exit codes: 0 SUCCESS  1 SETUP  3 a check failed.

using System;
using System.IO;

using MsgcoreCom;
using Msgc;
using static Msgc.Harness;

static class MgrPersistTest
{
    // =====================================================================
    // 1. A store is a heap that owns a tree
    // =====================================================================
    static void Demo_Store (Store store)
    {
        Section("1. MsgStore -- one heap, one tree");

        Check(store.Com.IsValid, "a fresh store is valid");
        Check(store.Com.Filename.Length == 0, "and has never been opened or saved");
        Check(store.Com.RootName.Length != 0, "but its root is named");

        Note("root name   : '{0}'", store.Com.RootName);
        Note("heap size   : {0} bytes", store.Com.Size);

        // The root is a node like any other -- which is the model's whole claim.
        var root = store.Root;
        Check(root != null, "the root is a node like any other");
        Check(root.Path.Length == 0, "and is the empty path");
        Check(root.Count == 0, "with no children yet");

        // THE ROOT'S NAME USED TO BE READ-ONLY AT THIS TIER, and the reason is
        // worth keeping because the kernel's spelling actively suggests
        // otherwise: P2PmsgMgr::Rename sits among the serialisation methods and
        // reads like a rename of the store, but it is a MoveFileEx on
        // m_strFilename. So it renamed the FILE, it took a PATH, and it answered
        // FALSE on a store that had never been saved -- and this tier published
        // it as RenameFile to say so.
        //
        // It is GONE. A host has its own file API, and what a client actually
        // wanted was the inverse of RootName, which is what RenameRoot is.
        Check(store.Com.RootName == "P2PmsgMgr name", "the default root name");
        store.Com.RenameRoot("Settings");
        Check(store.Com.RootName == "Settings", "RenameRoot renamed the ROOT NODE");

        // It is validated like any other name: 1 to 63 UTF-16 units, none of the
        // path grammar's separators.
        int hr = Api.Call(() => store.Com.RenameRoot("has.a.dot"));
        Check(hr == Hr.msgcName, "a separator in a name is msgcName");
        ShowError("RenameRoot with a separator in the name", hr);
        Check(store.Com.RootName == "Settings", "refused, not half-done");

        // IsValidName asks the same question WITHOUT writing anything, which is
        // what a form validating user input wants. New at this tier.
        Check(store.Com.IsValidName("Settings"), "a plain name is usable");
        Check(!store.Com.IsValidName("has.a.dot"), "one with a separator is not");
        Check(!store.Com.IsValidName(""), "nor is an empty one");
        Check(store.Com.IsValidName(new string('a', 63)), "63 units is in bounds");
        Check(!store.Com.IsValidName(new string('a', 64)), "64 is one over");

        store.Com.RenameRoot("P2PmsgMgr name");

        // Build something worth saving.
        var win = root.Declare("window", 0, true);
        win.Declare("width",  1024, true);
        win.Declare("height", 768, true);
        win.Declare("title",  "Ivyware Chartboard", true);

        var net = root.Declare("network", 0, true);
        net.Declare("host", "127.0.0.1", true);
        net.DeclareTyped("port", 7788, MsgDataType.UInt16, true);

        Check(root.Count == 2, "two top-level nodes");
        Check(Convert.ToInt32(store.Com.FieldAt(".window.width").Value) == 1024, "reachable by path");
        Check(store.Com.FieldAt(".network.port").TypeName == "UINT16", "with the declared width");

        // SIZE IS ALLOCATION, NOT OCCUPANCY. It starts at the heap's initial
        // size and only moves when the tree outgrows it, so a small tree in a
        // 4 KB heap reports 4 KB before and after. Reported as it is rather
        // than reinterpreted: a store's footprint is the honest answer to "how
        // big".
        int before = store.Com.Size;
        var bulk = root.Declare("bulk", 0, true);
        for (int i = 0; i < 200; ++i)
            bulk.Declare(string.Format("k{0:000}", i), "a reasonably long value string", true);
        int after = store.Com.Size;
        Check(after >= before, "the heap did not shrink");
        Note("heap grew   : {0} -> {1} bytes for 200 more nodes", before, after);

        bulk.Truncate();
        Check(bulk.Count == 0, "Truncate emptied the bulk node");
        Check(root.Delete("bulk"), "and it was removed");
    }


    // =====================================================================
    // 2. Save / Load -- the store as a document
    // =====================================================================
    static void Demo_SaveLoad (Store store)
    {
        Section("2. Save / Load -- the store as a document");

        string file = Scratch.Path("MgrPersistTestNet.p2p");
        Scratch.Remove(file);

        // Dirty is a flag the client can also drive, because "has this changed"
        // is a question only the client can finally answer.
        store.Com.Dirty = true;
        Check(store.Com.Dirty, "Dirty is settable from here");

        // Save with no path on a store that has never been given one is
        // msgcSave, not a file called "" somewhere.
        int hr = Api.Call(() => store.Com.Save(""));
        Check(hr == Hr.msgcSave, "Save() with no path and no Filename is msgcSave");
        ShowError("Save() with no path and no Filename", hr);

        store.Com.Save(file);
        Check(store.Com.Filename == file, "Filename followed the Save");
        Check(File.Exists(file), "and the file is on disk");
        Note("saved to    : {0}", file);

        // Now the no-argument form writes back over Filename.
        store.Root.Child("window").Child("width").Value = 1280;
        store.Com.Save("");

        // A SECOND, INDEPENDENT DOCUMENT. Two CoCreateInstances share nothing
        // -- no heap, no lock, no event queue.
        int hr2;
        using (var other = Store.Create(out hr2))
        {
            Check(other != null, "a second store");
            Check(other.Root.Count == 0, "which starts empty");
            other.Com.Open(file);

            Check(other.Com.RootName == store.Com.RootName, "the root name came back");
            Check(other.Root.Count == 2, "and both top-level nodes");
            Check(Convert.ToInt32(other.Com.FieldAt(".window.width").Value) == 1280, "with the saved value");
            Check((string)other.Com.FieldAt(".window.title").Value == "Ivyware Chartboard", "and the string");
            Check((string)other.Com.FieldAt(".network.host").Value == "127.0.0.1", "and the other string");

            // THE DECLARED WIDTH SURVIVED THE ROUND TRIP, which is the whole
            // reason DeclareTyped exists: a config round trip that normalised
            // UINT16 to INT32 would have rewritten the file just by reading and
            // re-saving it.
            Check(other.Com.FieldAt(".network.port").TypeName == "UINT16", "UINT16 survived the file");
            Check(Convert.ToInt32(other.Com.FieldAt(".network.port").Value) == 7788, "with its value");

            // The two are genuinely independent afterwards.
            other.Com.FieldAt(".window.width").Value = 640;
            Check(Convert.ToInt32(other.Com.FieldAt(".window.width").Value) == 640, "the copy changed");
            Check(Convert.ToInt32(store.Com.FieldAt(".window.width").Value) == 1280, "the original did not");

            // Clear -- was Nullify -- empties a store and keeps it open, which
            // is distinct from Close, which destroys it.
            //
            // TWO THINGS ABOUT IT CHANGED. It KEEPS ITS FILENAME now, because it
            // is the same store rather than a rebuilt one, so a "New" command in
            // a host does not silently lose the document it was editing. And a
            // node reference a client is holding stays valid and reports
            // msgcStale rather than pointing at a store that no longer exists.
            //
            // (The rebuild still happens; it happens one layer down, once for
            // every client. The core's own Nullify closes the heap and leaves
            // the manager pointing at nothing while still answering is_valid.)
            var heldBefore = other.Com.FieldAt(".window.width");
            Check(heldBefore != null, "a node reference to hold across the Clear");

            other.Com.Clear();
            Check(other.Com.IsValid, "still valid after Clear");
            Check(other.Root.Count == 0, "but empty");
            Check(other.Com.Filename == file, "and it KEPT its filename");

            object goneValue;
            Check(Api.Call(() => heldBefore.Value, out goneValue) == Hr.msgcStale,
                  "the held reference is msgcStale -- the node it named is gone");

            // And the store is genuinely usable, not merely valid-looking.
            other.Root.Declare("afterClear", 1, true);
            Check(other.Root.Count == 1, "and it takes new content");
        }

        // A file that is not a Msgcore image is refused at its synchronisation
        // word rather than half-loaded.
        string junk = Scratch.Path("MgrPersistTestNet.junk");
        File.WriteAllText(junk, "not a p2p image at all, not even close");
        int hr3;
        using (var bad = Store.Create(out hr3))
        {
            hr = Api.Call(() => bad.Com.Open(junk));
            Check(hr == Hr.msgcLoad, "a text file is msgcLoad");
            ShowError("Open(a text file)", hr);
        }
        Scratch.Remove(junk);

        // A path that does not exist is the same code, and says the same thing.
        int hr4;
        using (var missing = Store.Create(out hr4))
        {
            hr = Api.Call(() => missing.Com.Open(@"Z:\no\such\place\nothing.p2p"));
            Check(hr == Hr.msgcLoad, "and so is a path that does not exist");
        }

        Scratch.Remove(file);
    }


    // =====================================================================
    // 3. P2Pos -- addressing a node by position
    // =====================================================================
    //
    // A P2Pos is the identity MSGCORE ITSELF uses: it is what a trigger carries,
    // and it is the only thing a DELETED node still has. Both identities this
    // layer publishes come back through FieldAt.
    //
    // THIS SECTION USED TO BE ABOUT AN ASYMMETRY, and the asymmetry is gone. A
    // path answered a writable node; a P2Pos answered a DETACHED COPY whose
    // writes vanished, because the deep copy was all the layer below had. Both
    // spellings now arrive at the same kind of thing, because a position is
    // searched for once and answered as a ROUTE.
    //
    static void Demo_P2Pos (Store store)
    {
        Section("3. P2Pos -- the identity Msgcore itself uses");

        var width = store.Com.FieldAt(".window.width");
        Check(width != null, "reached by path");

        long pos = width.P2Pos;
        Check(pos != 0, "and it has a position");
        Note(".window.width: P2Pos={0}", pos);

        // It is STABLE across unrelated mutations -- that is what makes it an
        // inode.
        store.Root.Declare("unrelated", 1, true);
        Check(store.Com.FieldAt(".window.width").P2Pos == pos, "the position survived an unrelated write");

        // PATHOF ROUND-TRIPS NOW, which it did not. It used to answer MSGCORE's
        // own path spelling -- a leading '.' and the root's NAME as the first
        // segment -- so the two looked similar enough to be mistaken for each
        // other and feeding this one back to FieldAt found nothing. The doc
        // comment had to say so. Both ends are the same grammar.
        string own = store.Com.PathOf(pos);
        Check(own == ".window.width", "PathOf answers the published grammar");
        Check(own == width.Path, "the same one Path answers");
        Note("PathOf = Path = '{0}', and FieldAt takes it back", own);

        var byOwnPath = store.Com.FieldAt(own);
        Check(byOwnPath.P2Pos == pos, "and it resolves to the same node");

        // A STRING resolves the node.
        var live = store.Com.FieldAt(".window.width");
        live.Value = 1600;
        Check(Convert.ToInt32(store.Com.FieldAt(".window.width").Value) == 1600, "a path is writable");

        // A NUMBER resolves the SAME node, and a write through it lands.
        var byPos = store.Com.FieldAt(pos);
        Check(Convert.ToInt32(byPos.Value) == 1600, "a P2Pos reads the same value");
        Check(byPos.Path == ".window.width", "and knows the same path");
        byPos.Value = 1920;
        Check(Convert.ToInt32(store.Com.FieldAt(".window.width").Value) == 1920,
              "a write through a P2Pos lands -- it used to vanish");
        Check(Convert.ToInt32(live.Value) == 1920, "one node, two references");

        // And the path read off it goes straight back in, which is the round
        // trip PathOf could not do before.
        var writable = store.Com.FieldAt(byPos.Path);
        Check(Convert.ToInt32(writable.Value) == 1920, "the path off a P2Pos resolves");

        // THREE QUESTIONS, THREE ANSWERS: a position from nowhere, a path that
        // resolves to nothing, and a string that is not a path at all.
        IMsgFieldCom gone;
        int hr = Api.Call(() => store.Com.FieldAt(0x7FFFFFFFL), out gone);
        Check(hr == Hr.msgcNoPos, "a position from nowhere is msgcNoPos");
        ShowError("FieldAt(a P2Pos from nowhere)", hr);

        IMsgFieldCom nope;
        hr = Api.Call(() => store.Com.FieldAt(".window.depth"), out nope);
        Check(hr == Hr.msgcNoField, "a name from nowhere is msgcNoField");

        IMsgFieldCom unparseable;
        hr = Api.Call(() => store.Com.FieldAt("window.width"), out unparseable);
        Check(hr == Hr.msgcPath, "and a string with no leading separator is msgcPath");
        ShowError("FieldAt(\"window.width\") -- no leading separator", hr);

        // A DELETED node's P2Pos is a clean msgcNoPos rather than the debug
        // break it used to be: the search that resolves it walks the LIVE tree,
        // so a freed block is simply not found. That was the one sharp edge this
        // layer could not file off, and it is filed off.
        store.Root.Declare("doomed", 1, true);
        long doomedPos = store.Com.FieldAt(".doomed").P2Pos;
        Check(doomedPos != 0, "a node to delete");
        Check(store.Root.Delete("doomed"), "deleted");
        IMsgFieldCom deleted;
        hr = Api.Call(() => store.Com.FieldAt(doomedPos), out deleted);
        Check(hr == Hr.msgcNoPos, "and its P2Pos no longer resolves -- cleanly");

        Check(store.Root.Delete("unrelated"), "tidy up");
    }


    // =====================================================================
    // 4. Triggers -- headless change notification
    // =====================================================================
    //
    // Arm a node by P2Pos and OnChange is raised for it. DELETE fires by
    // itself when a node is freed; INSERT and UPDATE fire when a WRITER calls
    // FireTrigger. That is the flat ABI's rule unchanged -- Msgcore does not
    // detect its own mutations, it reports the ones it is told about -- and it
    // is worth stating, because "I armed it and nothing happened" is otherwise
    // a long afternoon.
    //
    // THE PATH TRAVELS WITH THE EVENT because it is resolvable only while the
    // mutation is still in progress. By the time the handler runs the node may
    // be gone, and for a DELETE it always is.
    //
    static void Demo_Triggers (Store store)
    {
        Section("4. Triggers -- headless change notification, replayed and marshalled");

        var sink = store.Sink();
        Check(sink != null, "the connection point accepted a managed sink");
        Note("A managed sink connects only because CMsgStore::Advise tries the DIID");
        Note("after IID_IDispatch fails -- ClassInterface(None) refuses the latter.");

        var watched = store.Root.Declare("watched", 100, true);
        long pos = watched.P2Pos;
        Check(pos != 0, "the watched node has a position");

        store.Com.ArmTrigger((int)MsgTriggerFlag.All, pos);

        // Nothing has been REPORTED yet, however much we mutate: Msgcore does
        // not detect its own writes.
        store.Com.FieldAt(".watched").Value = 200;
        Pump.For(200);
        Check(sink.Seen.Length == 0, "a silent write: armed, mutated, 0 events");
        Note("FireTrigger is the report -- Msgcore does not detect its own writes.");

        // The writer reports it. FireTrigger answers how many registrations
        // ran, which is the flat ABI's return value carried straight through.
        int fired = store.Com.FireTrigger((int)MsgTriggerFlag.Update, pos);
        Check(fired >= 1, "FireTrigger ran at least one registration");

        // AND NOW WE MUST PUMP. The event was queued on the mutating thread and
        // is replayed on the store's dispatch thread; a WaitForSingleObject
        // here would be a bet on the sink being agile, which is true today and
        // is not the contract.
        Check(sink.Arrived.Wait(1, 5000), "the event arrived");
        var seen = sink.Seen;
        Check(seen.Length == 1, "exactly one");
        if (seen.Length == 1)
        {
            Check(seen[0].Kind == (int)MsgTriggerFlag.Update, "of the kind that was fired");
            Check(seen[0].P2Pos == pos, "for the node that was armed");
            // The same spelling FieldAt takes, like every other path here -- it
            // used to be Msgcore's own, with the root's NAME as its first
            // segment, so an event handler could not resolve what it was given.
            Check(seen[0].Path == ".watched", "carrying the path FieldAt takes back");
            Note("OnChange    : kind={0} p2pos={1} path='{2}'",
                 seen[0].Kind, seen[0].P2Pos, seen[0].Path);
        }

        // --- WHERE THE HANDLER RAN, which is this tree's own finding ----------
        //
        // The C++ COM harness sees this event on its main thread: a C++ sink is
        // apartment-bound, so the GIT hands the dispatch thread a proxy and the
        // call marshals back into the STA. A managed CCW aggregates the
        // free-threaded marshaler and IS agile, so the GIT hands back the same
        // pointer and the handler runs where the dispatch loop is.
        if (seen.Length == 1)
        {
            Check(seen[0].Tid != Harness.MainTid,
                  "a managed sink is AGILE, so OnChange ran on the dispatch thread");
            Note("handler tid : {0}   main tid: {1}   (the C++ tree sees one number here)",
                 seen[0].Tid, Harness.MainTid);
        }

        // The handler may call back into the store, because the dispatch thread
        // does not hold the store's lock while it fires. This is the case that
        // would deadlock if the flat sink were called through directly.
        sink.Clear();
        int reentrantValue = 0;
        sink.Hook = c =>
        {
            var f = store.Com.FieldAt(c.P2Pos);
            if (f != null) reentrantValue = Convert.ToInt32(f.Value);
        };
        Check(store.Com.FireTrigger((int)MsgTriggerFlag.Insert, pos) >= 1, "fired an INSERT");
        Check(sink.Arrived.Wait(1, 5000), "which arrived");
        Check(sink.Seen.Length == 1 && sink.Seen[0].Kind == (int)MsgTriggerFlag.Insert,
              "as an INSERT");
        Check(reentrantValue == 200, "and the handler read back into the store without deadlocking");
        sink.Hook = null;

        // DELETE is the one that fires BY ITSELF -- and the one with a rule
        // attached, which this section exists to state.
        //
        // The DELETE trigger is fired from inside P2PmsgHeap_FreeBSTRio AFTER
        // the block has been freed and collated. So at the only moment a path
        // could have been captured, the node was already gone: `path` is ALWAYS
        // empty for a delete, and that is a fact about Msgcore's ordering
        // rather than an omission in the event.
        //
        // Which makes the P2Pos the whole of the answer -- and it is an
        // IDENTITY to COMPARE, not something to resolve. Handing it to FieldAt
        // reaches P2PmsgHeap_AssertValidAllocBSTRio, which finds a freed block
        // and trips ASSERT(0): a debug break in a Debug Msgcore and undefined
        // in a Release one. There is no "is this position still allocated"
        // predicate in the flat ABI, so no layer above it can check for you.
        // THIS HARNESS THEREFORE DOES NOT DO IT, deliberately.
        sink.Clear();
        Check(store.Root.Delete("watched"), "delete the watched node");
        Check(sink.Arrived.Wait(1, 5000), "the DELETE fired by itself");
        seen = sink.Seen;
        Check(seen.Length >= 1, "at least one event");
        if (seen.Length >= 1)
        {
            Check(seen[0].Kind == (int)MsgTriggerFlag.Delete, "of kind DELETE");
            Check(seen[0].P2Pos == pos, "carrying the position that was armed");
            Check(seen[0].Path.Length == 0, "and an empty path -- always, for a delete");
            Check(!store.Root.Exists("watched"), "the node really is gone");
            Note("OnChange    : kind=DELETE p2pos={0}, path empty -- the block was", seen[0].P2Pos);
            Note("              already freed when the trigger fired");
        }

        // Disarm, and the reports stop. (FireTrigger on an unarmed node is not
        // an error -- there is simply nothing registered to run.)
        var again = store.Root.Declare("watched2", 1, true);
        long pos2 = again.P2Pos;
        store.Com.ArmTrigger((int)MsgTriggerFlag.Update, pos2);
        store.Com.DisarmTrigger((int)MsgTriggerFlag.Update, pos2);

        sink.Clear();
        store.Com.FireTrigger((int)MsgTriggerFlag.Update, pos2);
        Pump.For(300);
        Check(sink.Seen.Length == 0, "a disarmed node reports nothing");
        Check(sink.Errors.Length == 0, "and no OnError was raised along the way");

        // After Unadvise nothing is delivered at all, which is the other half
        // of the connection-point contract.
        store.Unsink();
        store.Com.ArmTrigger((int)MsgTriggerFlag.Update, pos2);
        sink.Clear();
        store.Com.FireTrigger((int)MsgTriggerFlag.Update, pos2);
        Pump.For(200);
        Check(sink.Seen.Length == 0, "and after Unadvise, nothing at all");
    }


    // =====================================================================
    // 5. Paging -- a synchronous sink, called with the core blocked
    // =====================================================================
    //
    // A store can delegate the residency of a subtree to its host, so it may
    // describe far more data than it holds. The sink that does it is the
    // opposite of the OnChange sink at every point that matters:
    //
    //                   OnChange                 OnPageIn / OnPageOut
    //   raised on       a dispatch thread        the ACCESSING thread
    //   timing          after the fact           during, core is BLOCKED
    //   store lock      not held                 HELD
    //   return value    ignored                  the answer; False = failed
    //   may block       yes                      no
    //
    // A page-in that has not returned is data that is not there, so there is no
    // version of this that defers -- the queueing that makes OnChange safe
    // cannot be applied. That is also why it is a directly-registered sink
    // object rather than a source dispinterface: a dispinterface cannot
    // usefully return a value across a marshalling boundary, and marshalling a
    // call the core is synchronously waiting on, under a lock it already holds,
    // is a deadlock.
    //
    // SO SECTIONS 4 AND 5 MEASURE THE SAME AGILITY FROM OPPOSITE SIDES: it is
    // what puts OnChange on a thread the client never made, and it is what
    // lets the paging sink be called with no marshalling at all.
    //
    static void Demo_Paging (Store store)
    {
        Section("5. Paging -- a synchronous sink, called with the core blocked");

        var f = store.Root.Declare("paged", 0, true);
        long pos = f.P2Pos;
        Check(pos != 0, "a node to page");

        // With no sink installed, driving paging is a successful no-op rather
        // than an error: a store that does not page is the ordinary case.
        Check(Api.Call(() => store.Com.PageIn(pos)) == Hr.S_OK, "PageIn with no sink is a no-op, not an error");

        var sink = new PagingSink();
        store.Com.SetPagingSink(sink);

        Check(store.Com.PageIn(pos), "PageIn answered True");
        Check(sink.PageIns == 1, "and the sink had ALREADY run by the time it returned");
        Note("the sink had already run by the time PageIn returned");

        // And it ran on THIS thread. That is the contract -- there is no
        // dispatch thread involved and no marshalling, which is exactly what a
        // call the core is blocked inside requires.
        Check(sink.LastTid == Harness.MainTid, "on the calling thread, not a dispatch thread");

        Check(store.Com.PageOut(pos, true), "PageOut answered True");
        Check(sink.PageOuts == 1, "the sink ran");
        Check(sink.LastFlush, "and the flush flag crossed intact");

        store.Com.PageOut(pos, false);
        Check(!sink.LastFlush, "as did False");

        // The P2Pos crossed as a VARIANT and came back unchanged -- but NOT as
        // the same VARIANT type the event carries, and this tier is the first
        // one that can tell.
        //
        // _IMsgStoreEvents::OnChange declares its p2pos as `hyper`, so it is
        // VT_I8 and arrives as a CLR long. The paging sink's is a VARIANT the
        // server fills in, and it fills it as VT_UI8 -- so it arrives as a CLR
        // ULONG. A C++ handler never notices, because it reads whichever field
        // it asks for; a C# one that writes `(long)p2pos` gets an
        // InvalidCastException. Both are the same 64-bit position.
        Check(sink.LastPos is ulong, "the paging position crossed as VT_UI8, not VT_I8");
        Check(Convert.ToInt64(sink.LastPos) == pos, "with the same value as the node's P2Pos");
        Note("OnChange carries VT_I8 (long); the paging sink carries VT_UI8 (ulong).");
        Note("Convert.ToInt64 is right for both; a cast is right for neither.");

        // A sink that refuses is reported to the caller that provoked it, not
        // raised as an exception: the core cannot unwind through a page fault.
        sink.Answer = false;
        Check(!store.Com.PageIn(pos), "a refusal is reported as False to the caller");
        sink.Answer = true;

        // --- suspend and restore ---------------------------------------------
        // What the C++ SafeRegistrationPush does with a constructor and a
        // destructor. A Save is the usual reason: it walks everything and must
        // not fault the whole store in on the way past.
        sink.Reset();
        store.Com.PushPaging();

        // Push does NOT nest. The core saves one registration, so a second push
        // would discard the first and the matching pop restore the wrong set --
        // it asserts in a debug core and loses it silently in a release one.
        // Refused here so both builds behave alike and the caller is told.
        int hr = Api.Call(() => store.Com.PushPaging());
        Check(hr == Hr.msgcPageState, "a second PushPaging is msgcPageState");
        ShowError("a second PushPaging", hr);

        store.Com.PopPaging();
        Check(Api.Call(() => store.Com.PopPaging()) == Hr.msgcPageState, "and there is nothing left to restore");

        // --- unregister --------------------------------------------------------
        store.Com.SetPagingSink(null);
        sink.Reset();
        store.Com.PageIn(pos);
        Check(sink.PageIns == 0, "a cleared sink really is unhooked");

        Note("A paging handler must not block and must not re-enter the store");
        Note("beyond the subtree it was asked for: it runs under the store lock.");
    }


    // =====================================================================
    // main
    // =====================================================================
    [STAThread]
    static int Main ()
    {
        InitConsole();
        Console.WriteLine("=== MgrPersistTestNet - a store as a document, from C# ===");

        int hr;
        using (var store = Store.Create(out hr))
        {
            if (store == null) return SetupFailure("new MsgcoreCom.MsgStore", hr);

            Demo_Store    (store);
            Demo_SaveLoad (store);
            Demo_P2Pos    (store);
            Demo_Triggers (store);
            Demo_Paging   (store);
        }

        return Verdict();
    }
}
