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
// BstrWidthTest.cs
//
// The heap under everything else, and its one irreversible decision. The
// counterpart of ..\ComExamples\BstrWidthTestCom.
//
// THE SUBJECT of the C++ original is that P3PmsgField16, P3PmsgField32 and
// P3PmsgField64 are three DIFFERENT TYPES, so a C++ caller who wants to handle
// a store of any width writes a template or writes it three times. Above the
// flat ABI that distinction is gone: there is one IMsgFieldCom, the width never
// appears in a signature, and section 4 is what that is worth.
//
// The width is still real, and still irreversible -- it decides how wide every
// internal reference in the heap is and therefore the on-disk layout. That is
// why CreateNew is the only place it can be set, and why there is no property
// for it: a property implies it could be assigned.
//
// Exit codes: 0 SUCCESS  1 SETUP  3 a check failed.

using System;
using System.IO;

using MsgcoreCom;
using Msgc;
using static Msgc.Harness;

static class BstrWidthTest
{
    // The same tree every time, so the three widths are compared on identical
    // content and any difference in Size is the ADDRESSING and nothing else.
    static void FillSampleTree (Store store, int leaves)
    {
        var cfg = store.Root.Declare("cfg", 0, true);
        for (int i = 0; i < leaves; ++i)
            cfg.Declare(string.Format("key{0:000}", i), i * 7, true);
    }

    static bool VerifySampleTree (Store store, int leaves)
    {
        if (store.Com.FieldAt(".cfg").Count != leaves) return false;
        for (int i = 0; i < leaves; ++i)
        {
            var f = store.Com.FieldAt(string.Format(".cfg.key{0:000}", i));
            if (Convert.ToInt32(f.Value) != i * 7) return false;
        }
        return true;
    }


    // =====================================================================
    // 1. The heap under everything else
    // =====================================================================
    static void Demo_Heap ()
    {
        Section("1. The heap -- allocation, growth, and what Size means");

        int hr;
        using (var store = Store.Create(out hr))
        {
            Check(store != null, "a default store");

            int empty = store.Com.Size;
            Check(empty > 0, "which has a heap");
            Note("default heap: {0} bytes empty", empty);

            // SIZE IS ALLOCATION, NOT OCCUPANCY, and this is the cleanest place
            // to see it: a handful of nodes do not move the number at all,
            // because they fit in what was already reserved.
            FillSampleTree(store, 5);
            Check(store.Com.Size == empty, "5 nodes fit in the initial reservation");
            Note("5 nodes     : still {0} bytes", store.Com.Size);

            // Outgrow it, and it moves.
            var cfg = store.Com.FieldAt(".cfg");
            for (int i = 0; i < 400; ++i)
                cfg.Declare(string.Format("pad{0:000}", i),
                            "padding padding padding padding", true);
            int grown = store.Com.Size;
            Check(grown > empty, "405 nodes did not");
            Note("405 nodes   : {0} bytes", grown);

            // AND IT DOES NOT SHRINK. Truncate frees the nodes back into the
            // heap's own free list, not back to the process -- which is exactly
            // what makes the heap relocation-safe and a Save a plain block
            // write. Worth pinning, because "I deleted everything and Size did
            // not change" reads as a leak and is not one.
            cfg.Truncate();
            Check(cfg.Count == 0, "Truncate emptied it");
            Check(store.Com.Size == grown, "and the heap did not shrink");
            Note("after Truncate: still {0} bytes -- freed into the heap, not the process",
                 store.Com.Size);
        }
    }


    // =====================================================================
    // 2. The addressing width, chosen once
    // =====================================================================
    static void Demo_Widths ()
    {
        Section("2. msgcAddr16 / 32 / 64 -- fixed once, at CreateNew");

        var widths = new[] { MsgAddrMode.Addr16, MsgAddrMode.Addr32, MsgAddrMode.Addr64 };
        var names  = new[] { "Addr16", "Addr32", "Addr64" };

        for (int i = 0; i < 3; ++i)
        {
            int hr;
            using (var s = Store.Create(out hr))
            {
                s.Com.CreateNew(widths[i], 0, 0);
                Check(s.Com.IsValid, names[i] + " store is valid");

                // A store of any width is a store: the whole API is
                // width-agnostic, which is the claim C++'s P3PmsgField16 / 32 /
                // 64 typedefs make by being interchangeable in everything but
                // their names.
                FillSampleTree(s, 20);
                Check(VerifySampleTree(s, 20), "and holds the same tree");
                Check(s.Root.Count == 1, "with one top-level node");
                Check(Convert.ToInt32(s.Com.FieldAt(".cfg.key007").Value) == 49, "and the same values");
                Check(s.Com.FieldAt(".cfg.key007").TypeName == "INT32", "and the same types");

                Note("{0}      : 20 nodes, heap {1} bytes", names[i], s.Com.Size);
            }
        }

        // An unrecognised mode is refused rather than silently defaulted -- a
        // store built at the wrong width is a file nobody can read back.
        int hr2;
        using (var bad = Store.Create(out hr2))
        {
            Check(Api.Call(() => bad.Com.CreateNew(0, 0, 0)) == Hr.E_INVALIDARG, "CreateNew(0) is refused");
            int hr3 = Api.Call(() => bad.Com.CreateNew(9, 0, 0));
            Check(hr3 == Hr.E_INVALIDARG, "CreateNew(9) too");
            ShowError("CreateNew(9)", hr3);

            // ... and the store it was called on is untouched, because
            // CreateNew builds the replacement BEFORE destroying what is there.
            Check(bad.Com.IsValid, "and the store it was called on is untouched");
            Check(bad.Root != null, "with a usable root");
        }

        // Explicit initial and maximum sizes. The initial one is observable
        // immediately, which is the whole reason to pass it: a store that is
        // going to hold a megabyte should not grow to it in fifty steps.
        int hr4;
        using (var sized = Store.Create(out hr4))
        {
            sized.Com.CreateNew(MsgAddrMode.Addr32, 32768, 1048576);
            Check(sized.Com.Size >= 32768, "an explicit reservation is honoured immediately");
            Note("Addr32 with an explicit 32 KB reservation: {0} bytes empty", sized.Com.Size);

            FillSampleTree(sized, 50);
            Check(VerifySampleTree(sized, 50), "and it still holds a tree");
            Check(sized.Com.Size >= 32768, "without shrinking below the reservation");
        }
    }


    // =====================================================================
    // 3. The same tree at three widths, and through a file
    // =====================================================================
    //
    // The interesting claim is not that each width works on its own -- it is
    // that the WIDTH IS PART OF THE FILE, and that a reader does not have to be
    // told which one it is getting.
    //
    static void Demo_ThreeWidthsThroughAFile ()
    {
        Section("3. The same tree at three widths, saved and read back");

        var widths = new[] { MsgAddrMode.Addr16, MsgAddrMode.Addr32, MsgAddrMode.Addr64 };
        var names  = new[] { "Addr16", "Addr32", "Addr64" };
        var files  = new[] { "BstrWidthTestNet16.p2p", "BstrWidthTestNet32.p2p", "BstrWidthTestNet64.p2p" };
        var onDisk = new long[3];

        for (int i = 0; i < 3; ++i)
        {
            string path = Scratch.Path(files[i]);
            Scratch.Remove(path);

            int hr;
            using (var s = Store.Create(out hr))
            {
                s.Com.CreateNew(widths[i], 0, 0);
                FillSampleTree(s, 40);
                s.Com.Save(path);
                Check(s.Com.Filename == path, names[i] + " saved");
            }

            onDisk[i] = new FileInfo(path).Length;

            // A PLAIN, DEFAULT STORE READS IT. The reader is not told the width
            // and does not have to be: it is in the image. That is what makes a
            // .p2p portable between a 16-bit writer and a 64-bit reader.
            int hr2;
            using (var reader = Store.Create(out hr2))
            {
                reader.Com.Open(path);
                Check(VerifySampleTree(reader, 40), "a default store read it back");
                Check(Convert.ToInt32(reader.Com.FieldAt(".cfg.key039").Value) == 273, "down to the last leaf");
            }

            Note("{0}      : {1} bytes on disk, read back by a default store", names[i], onDisk[i]);
            Scratch.Remove(path);
        }

        // The narrow store really is narrower -- if all three were the same
        // size, the mode would be doing nothing.
        Check(onDisk[0] > 0 && onDisk[1] > 0 && onDisk[2] > 0, "all three files exist");
        Check(onDisk[0] < onDisk[2], "and the 16-bit image really is smaller than the 64-bit one");
    }


    // =====================================================================
    // 4. There is no P3PmsgField16 here, and that is the point
    // =====================================================================
    static void Demo_NoWidthInTheType ()
    {
        Section("4. A node is a node -- the width is not in its type");

        int hrN, hrW;
        using (var narrow = Store.Create(out hrN))
        using (var wide   = Store.Create(out hrW))
        {
            narrow.Com.CreateNew(MsgAddrMode.Addr16, 0, 0);
            wide.Com.CreateNew  (MsgAddrMode.Addr64, 0, 0);

            FillSampleTree(narrow, 10);
            FillSampleTree(wide,   10);

            // ONE METHOD, BOTH STORES -- FillSampleTree and VerifySampleTree
            // above take a Store and never mention a width. In C++ this is
            // where a caller discovers that P3PmsgField16 and P3PmsgField64 are
            // different types and their code has to be a template or duplicated.
            Check(VerifySampleTree(narrow, 10), "the 16-bit store verifies");
            Check(VerifySampleTree(wide,   10), "and so does the 64-bit one");

            var a = narrow.Com.FieldAt(".cfg.key003");
            var b = wide.Com.FieldAt(".cfg.key003");
            Check(Convert.ToInt32(a.Value) == Convert.ToInt32(b.Value), "same value");
            Check(a.TypeName == b.TypeName, "same type name");
            Check(a.Path == b.Path, "same path");

            // A node of one store and a node of another are still different
            // objects over different heaps, and MoveChild is where that has to
            // be ENFORCED rather than merely believed.
            //
            // A node reference here is a PATH, so a destination from ANOTHER
            // store would have its path resolved in THIS one -- and these two
            // stores have the same shape, so "cfg" resolves in both. Without an
            // identity check the move would succeed, silently, against the
            // wrong tree (here, against itself). So it is refused, and
            // msgcForeign is the code to branch on.
            var nCfg = narrow.Com.FieldAt(".cfg");
            var wCfg = wide.Com.FieldAt(".cfg");
            int narrowBefore = nCfg.Count;
            int wideBefore   = wCfg.Count;

            int hr = Api.Call(() => nCfg.MoveChild(wCfg, "key003"));
            Check(hr == Hr.msgcForeign, "a cross-store MoveChild is msgcForeign");
            ShowError("MoveChild into another store", hr);

            Check(wide.Com.FieldAt(".cfg").Count == wideBefore, "the destination did not gain one");
            Check(narrow.Com.FieldAt(".cfg").Count == narrowBefore, "the source did not lose one");
            Note("a cross-store MoveChild is refused, and neither store moved");

            // A move to where the child already is changes nothing -- the flat
            // move is a remove-and-re-add, so performing it would churn the
            // child's position for no reason. The server answers S_FALSE, which
            // a managed caller cannot see (a success code has nowhere to go);
            // what IS observable is that the count did not move.
            Check(Api.Call(() => nCfg.MoveChild(nCfg, "key003")) == Hr.S_OK,
                  "a move to where it already is does not fail");
            Check(narrow.Com.FieldAt(".cfg").Count == narrowBefore, "and changes nothing");
            Note("S_FALSE is invisible to a .NET caller; the count is the observable.");

            // A real move WITHIN one store still works, which is what makes the
            // two refusals above meaningful rather than a blanket ban.
            Check(nCfg.MoveChild(narrow.Root, "key003"), "a move within one store works");
            Check(narrow.Com.FieldAt(".cfg").Count == narrowBefore - 1, "the source lost one");
            Check(Convert.ToInt32(narrow.Com.FieldAt(".key003").Value) == 21, "and the root gained it");
        }
    }


    // =====================================================================
    // 5. Bounding a heap -- and the paging hooks, which are no longer absent
    // =====================================================================
    //
    // This section used to record that the paging hooks did not cross: they let
    // a host supply its own backing for a heap by handing Msgcore FUNCTION
    // POINTERS, and there is no VARIANT type for one. That reasoning was about
    // the wrong thing. A sink OBJECT is not a function pointer, the flat ABI
    // grew msgcore_mgr_set_paging_sinks, and MgrPersistTest section 5 drives the
    // whole facility -- SetPagingSink, PageIn, PageOut, PushPaging -- from
    // managed code.
    //
    // What is left here is the OTHER half of the same question, and it is the
    // half most callers actually want: bounding a heap without writing a sink
    // at all. CreateNew's two size arguments say how much backing a store
    // should have and how much it may ever take, and unlike a paging handler
    // they cost the client nothing to get right and cannot deadlock the store.
    //
    static void Demo_BoundedHeap ()
    {
        Section("5. Bounding a heap without a sink -- CreateNew's two sizes");

        int hr;
        using (var bounded = Store.Create(out hr))
        {
            bounded.Com.CreateNew(MsgAddrMode.Addr32, 4096, 65536);
            Check(bounded.Com.Size >= 4096, "the initial reservation is honoured");

            var cfg = bounded.Root.Declare("cfg", 0, true);

            // Push until it either fills or refuses. Whichever happens, the
            // store is still usable afterwards and says so -- which is the
            // property that matters to a client, and the one a raw paging
            // callback would have made the client responsible for.
            int stored = 0;
            bool refused = false;
            for (int i = 0; i < 4000 && !refused; ++i)
            {
                int h = Api.Call(() => cfg.Declare(string.Format("k{0:0000}", i),
                                                   "0123456789012345678901234567890123456789", true));
                if (Hr.Failed(h)) refused = true; else ++stored;
            }

            Note("bounded at 64 KB: stored {0} nodes, heap {1} bytes, refused={2}",
                 stored, bounded.Com.Size, refused ? "yes" : "no");

            Check(stored > 0, "something was stored");
            Check(bounded.Com.IsValid, "and the store is still valid");
            Check(bounded.Root != null, "with a usable root");

            // Whatever it did store is intact and readable -- a bounded heap is
            // not a corrupted one.
            Check(bounded.Com.FieldAt(".cfg").Count == stored, "every stored node is still there");
            Check(((string)bounded.Com.FieldAt(".cfg.k0000").Value).Length == 40, "with its value intact");

            // And it still saves and reloads, which is the real test of "still
            // usable".
            string path = Scratch.Path("BstrWidthTestNetBounded.p2p");
            Scratch.Remove(path);
            bounded.Com.Save(path);

            int hr2;
            using (var back = Store.Create(out hr2))
            {
                back.Com.Open(path);
                Check(back.Com.FieldAt(".cfg").Count == stored, "and reloads with the same count");
            }
            Scratch.Remove(path);
        }

        Note("The paging sink is no longer the missing half of this: see");
        Note("MgrPersistTest section 5, which drives it from managed code.");
    }


    // =====================================================================
    // main
    // =====================================================================
    [STAThread]
    static int Main ()
    {
        InitConsole();
        Console.WriteLine("=== BstrWidthTestNet - the heap, and its one irreversible decision ===");

        // Prove the server is reachable before any section runs, so a missing
        // registration is exit 1 and not a wall of failed checks.
        int hr;
        using (var probe = Store.Create(out hr))
        {
            if (probe == null) return SetupFailure("new MsgcoreCom.MsgStore", hr);
            Log("MAIN", "{0}", probe.Com.VersionString);
        }

        Demo_Heap                    ();
        Demo_Widths                  ();
        Demo_ThreeWidthsThroughAFile ();
        Demo_NoWidthInTheType        ();
        Demo_BoundedHeap             ();

        return Verdict();
    }
}
