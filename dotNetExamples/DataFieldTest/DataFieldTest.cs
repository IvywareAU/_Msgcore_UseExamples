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
// DataFieldTest.cs
//
// The Msgcore DATA MODEL from C#. The counterpart of
// ..\ComExamples\DataFieldTestCom, and the first thing to read
// in this tree.
//
// THIS ASSEMBLY REFERENCES mscorlib, System AND System.Core, AND NOTHING ELSE.
// No interop assembly, no PIA, no Msgcore.dll, no ATL, no MIDL output. Every
// object it touches is a coclass found in the registry at run time through the
// [ComImport] declarations in common\MsgcoreComInterop.cs. That is the
// distinguishing property of this tree, and it is why the comparison with the
// C++ COM harness next door is worth making: same subject, same server, a
// caller with a completely different runtime underneath.
//
// WHAT CHANGES WHEN THE CALLER IS MANAGED, in one paragraph. The BSTRs, the
// VARIANTs, the SAFEARRAYs and the reference counting all disappear -- the C++
// harness spends about a thousand lines of ComHarness.h on them and this tree
// spends none. What arrives instead is a different error channel: an HRESULT is
// an exception here, and the CLR REWRITES some of them into CLR exception types
// on the way (section 6). Everything else in this file is the same story told
// in a language where a node's value is just an object.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS   1 = SETUP (server not registered)   3 = a check failed

using System;
using System.Runtime.InteropServices;

using MsgcoreCom;
using Msgc;
using static Msgc.Harness;

static class DataFieldTest
{
    // =====================================================================
    // 1. Typed value cells
    // =====================================================================
    //
    // A Msgcore node is a discriminated value: a type tag plus storage sized
    // for that tag. In C++ each c_XXX() accessor asserts the tag matches, and
    // c_int() on a DOUBLE cell THROWS rather than truncating.
    //
    // Here there is one accessor, Value, and it is an `object` -- the VARIANT
    // already carried its own type and the marshaller has already turned it
    // into an int, a double, a string, a bool, a DateTime or a byte[]. The
    // whole strict-accessor problem is gone at this tier, and `is` and
    // Convert.* are what remain of it.
    //
    // What does NOT go away is the node's DECLARED WIDTH: no CLR type says
    // "UINT08", so DeclareTyped exists for the cases where the width is data.
    //
    static void Demo_TypedCells (Store store)
    {
        Section("1. Typed value cells -- one object instead of sixteen accessors");

        var cells = store.Root.Declare("cells", 0, true);
        Check(cells != null, "Declare returned the new node");

        // Integers. A value-typed declare always lands as INT32, which is the
        // flat ABI's msgcore_field_declare_int.
        cells.Declare("anInt", 12345, true);
        Check(Convert.ToInt32(cells.Child("anInt").Value) == 12345, "anInt reads back");
        Check(cells.Child("anInt").DataType == MsgDataType.Int32, "anInt is INT32");
        Check(cells.Child("anInt").TypeName == "INT32", "...and says so by name");

        // A write through the LIVE child reaches the store, and PRESERVES the
        // declared type rather than re-typing from the value.
        cells.Child("anInt").Value = 999;
        Check(Convert.ToInt32(cells.Child("anInt").Value) == 999, "the write landed");
        Check(cells.Child("anInt").DataType == MsgDataType.Int32, "and did not retype");

        // The declared WIDTH is the one thing an object cannot express, so it
        // has its own verb. A ".value" round trip that normalised UINT08 to
        // INT32 would have rewritten the store just by reading it.
        cells.DeclareTyped("aByte",  200, MsgDataType.UInt08, true);
        cells.DeclareTyped("aShort", 7,   MsgDataType.Int16,  true);
        cells.DeclareTyped("aBig",   9007199254740993L, MsgDataType.Int64, true);
        Check(cells.Child("aByte").TypeName  == "UINT08", "UINT08 width survived");
        Check(Convert.ToInt32(cells.Child("aByte").Value) == 200, "...with its value");
        Check(cells.Child("aShort").TypeName == "INT16", "INT16 width survived");
        Check(Convert.ToInt32(cells.Child("aShort").Value) == 7, "...with its value");

        // > 2^53. It comes back as a CLR long, not a double and not a truncated
        // int, because the server widens rather than clamping -- so the exact
        // integer survives a round trip that any JSON-shaped tier would lose.
        Check(cells.Child("aBig").TypeName == "INT64", "INT64 width survived");
        Check(cells.Child("aBig").Value is long, "and arrives as a CLR long, not a double");
        Check((long)cells.Child("aBig").Value == 9007199254740993L, "exact past 2^53");

        // Doubles and booleans have their own types, so no DeclareTyped.
        cells.Declare("aDouble", 2.5, true);
        Check((double)cells.Child("aDouble").Value == 2.5, "double reads back");
        Check(cells.Child("aDouble").TypeName == "DOUBLE", "as DOUBLE");

        cells.Declare("aBool", true, true);
        Check((bool)cells.Child("aBool").Value, "bool reads back");
        Check(cells.Child("aBool").TypeName == "BOOL", "as BOOL");
        cells.Child("aBool").Value = false;
        Check(!(bool)cells.Child("aBool").Value, "and can be written");

        // Wide strings. The storage is WSTR16 -- 16-bit units, which is what
        // the wire format and the Linux port both assume -- and a CLR string is
        // already exactly that, so the text crosses two boundaries (CLR ->
        // BSTR -> WSTR16) with no re-encoding at all. U+20AC proves it.
        cells.Declare("aStr", "Hello, €uro world", true);
        Check((string)cells.Child("aStr").Value == "Hello, €uro world", "wide string is intact");
        Check(cells.Child("aStr").TypeName == "WSTR16", "stored as WSTR16");
        Note("wstr cell   : \"{0}\"  (type={1})",
             cells.Child("aStr").Value, cells.Child("aStr").TypeName);

        // A blob is opaque bytes with a size -- no interpretation, no
        // terminator. A byte[] marshals into the VARIANT as SAFEARRAY(VT_UI1),
        // which is the one automation type that means "bytes" and nothing else.
        byte[] sample = new byte[sizeof(int) + sizeof(double)];
        Buffer.BlockCopy(BitConverter.GetBytes(42),  0, sample, 0, sizeof(int));
        Buffer.BlockCopy(BitConverter.GetBytes(3.5), 0, sample, sizeof(int), sizeof(double));

        cells.Declare("aBlob", sample, true);
        Check(cells.Child("aBlob").TypeName == "BLOB16", "byte[] stored as BLOB16");

        byte[] back = Api.AsBytes(cells.Child("aBlob").Value);
        Check(back.Length == sample.Length, "the blob came back the same size");
        if (back.Length == sample.Length)
        {
            Check(BitConverter.ToInt32(back, 0) == 42, "...and the same first field");
            Check(BitConverter.ToDouble(back, sizeof(int)) == 3.5, "...and the same second");
        }

        // A GUID has no VARIANT type, so it is carried as its canonical text --
        // which is what a caller compares, logs and puts in a registry key
        // anyway. Note that it does NOT arrive as a System.Guid: the marshaller
        // has no reason to know that a WSTR16 named aGuid is one.
        cells.DeclareTyped("aGuid", "3F2504E0-4F89-11D3-9A0C-0305E82C3301",
                           MsgDataType.Guid, true);
        Check(cells.Child("aGuid").TypeName == "GUID", "GUID width survived");
        Check((string)cells.Child("aGuid").Value == "3F2504E0-4F89-11D3-9A0C-0305E82C3301",
              "and reads back as canonical text");
        Check(Guid.Parse((string)cells.Child("aGuid").Value) != Guid.Empty,
              "which Guid.Parse accepts, so the conversion is the caller's to make");

        // FLOAT USED TO BE UNREACHABLE FROM HERE and is not any more. The flat
        // ABI had no declare for it -- msgcore_field_get_float existed, its
        // declare did not -- so this pair of checks used to assert that both
        // FLOAT and NULL were refused. DeclareTyped reaches the whole width
        // table now.
        cells.DeclareTyped("aFloat", 1.5, MsgDataType.Float, true);
        Check(cells.Child("aFloat").TypeName == "FLOAT", "FLOAT can be declared");
        Check(Convert.ToDouble(cells.Child("aFloat").Value) == 1.5, "and holds its value");

        // NULL is still refused, for a reason that has not changed: it is the
        // ABSENCE of a value, so "declare one" is not a request that means
        // anything.
        int hr = Api.Call(() => cells.DeclareTyped("aNull", 0, MsgDataType.Null, true));
        Check(hr == Hr.msgcType, "DeclareTyped(NULL) is refused");
        ShowError("DeclareTyped(NULL)", hr);

        // Text is Value SEEN AS a string, and is not a synonym: it renders a
        // numeric node with Msgcore's own formatting rules.
        Note("int as Text : '{0}'    double as Text: '{1}'",
             cells.Child("anInt").Text, cells.Child("aDouble").Text);
        Check(cells.Child("anInt").Text.Length != 0, "Text renders a numeric node");

        // The type vocabulary round-trips, so a ".type" file written from
        // TypeName reads back as the same code.
        Check(store.Com.TypeFromName("UINT64") == MsgDataType.UInt64, "TypeFromName round-trips");
        Check(store.Com.TypeName(MsgDataType.UInt64) == "UINT64", "...and back again");
        Check(store.Com.TypeFromName("NoSuchType") == -1, "an unknown type name is -1, not an error");
    }


    // =====================================================================
    // 2. A node is a name and a value
    // =====================================================================
    //
    // In C++ a P3PmsgField IS a P3PmsgName and a P3PmsgData glued together by
    // inheritance, which is why oField.c_int() and oField == L"Name" both work
    // on it directly. Here the same node is one object with a Name property and
    // a Value property, and "no value" shows up as IsNull rather than as an
    // assertion on first write.
    //
    // THIS SECTION USED TO BE HALF ABOUT DETACHED COPIES -- Item answered a deep
    // copy with independent storage, and a write through it succeeded and
    // reached nothing. That is gone; see the second half.
    //
    static void Demo_NameAndValue (Store store)
    {
        Section("2. A node -- a name, a value, and a reference that is a route");

        var people = store.Root.Declare("people", 0, true);
        var johnno = people.Declare("Johnno", 42, true);

        Check(johnno.Name == "Johnno", "the node knows its own name");
        Check(Convert.ToInt32(johnno.Value) == 42, "and its value");
        Check(!johnno.IsNull, "which is not null");

        // The identity a client should KEEP is the path, not the object. It is
        // a string, it survives anything, and MsgStore.FieldAt takes it back.
        //
        // THE SPELLING CHANGED WITH THE PORT: every step is INTRODUCED by its
        // separator -- '.' for a child, '@' for an attribute -- so the root is ""
        // and its child is ".people", not "people". The old dotted chain had no
        // leading separator and no way to name an attribute at all; this one is
        // reversible, which is the point.
        Check(johnno.Path == ".people.Johnno", "Path introduces every step with its separator");
        Check(Convert.ToInt32(store.Com.FieldAt(".people.Johnno").Value) == 42,
              "and FieldAt takes it straight back");

        // A path that does not PARSE says so, rather than answering "no such
        // node" and leaving the caller to wonder which of the two it was.
        object bad;
        Check(Api.Call(() => store.Com.FieldAt("people.Johnno"), out bad) == Hr.msgcPath,
              "the old dotted spelling is msgcPath");

        // Renaming is done through the PARENT, because a node does not own its
        // own name -- the collection it sits in does.
        Check(people.RenameChild("Johnno", "Larry"), "RenameChild reports it renamed one");
        Check(people.Exists("Larry"), "the new name is there");
        Check(!people.Exists("Johnno"), "the old one is not");
        Check(Convert.ToInt32(people.Child("Larry").Value) == 42, "and the value survived");
        Note("renamed to '{0}', value still {1}",
             people.Child("Larry").Name, people.Child("Larry").Value);

        // A HELD REFERENCE IS A PATH, NOT A POINTER, and that is visible here:
        // `johnno` names people.Johnno, which no longer exists, so it answers
        // msgcStale instead of reading a dangling heap address. Against the
        // flat ABI this is the case where a live handle has silently become
        // garbage.
        //
        // NOTE WHAT THAT MEANS IN C#: the property getter THROWS. A reference
        // held across a rename is not a stale value, it is an exception at the
        // next read, which is the behaviour a managed caller can actually
        // defend against.
        object v;
        int hr = Api.Call(() => johnno.Value, out v);
        Check(hr == Hr.msgcStale, "the renamed-away reference is msgcStale");
        ShowError("the renamed-away reference", hr);

        // TWO REFERENCES TO ONE NODE ARE TWO ROUTES TO ONE NODE.
        //
        // This is where Item used to be: a DETACHED deep copy whose whole
        // pedagogical job was to show that a write through it succeeded and
        // reached nothing -- the most expensive mistake this tier had to offer,
        // because it had no symptom until a Save came out without the change.
        // Item, IsLive and msgcDetached are all gone. What replaces them: get
        // the same node twice, by two different spellings, and both are real.
        var byName = people.Child("Larry");
        var byPath = store.Com.FieldAt(".people.Larry");
        Check(byName != null && byPath != null, "reached the same node two ways");
        Check(Convert.ToInt32(byName.Value) == 42, "both read the value");
        Check(byName.DataType == byPath.DataType, "and agree on the type");
        Check(byName.P2Pos == byPath.P2Pos, "and are the same node");

        byName.Value = 99;
        Check(Convert.ToInt32(byPath.Value) == 99, "a write through one is seen by the other");
        byPath.Value = 7;
        Check(Convert.ToInt32(byName.Value) == 7, "...in both directions");
        Check(Convert.ToInt32(people.Child("Larry").Value) == 7, "and by a third taken afterwards");

        // A caller who WANTS an independent snapshot takes one explicitly, which
        // is the honest spelling of what Item was doing by accident.
        object snapshot = byName.Value;
        byName.Value = 1234;
        Check(Convert.ToInt32(snapshot) == 7, "a snapshot is a VALUE, and does not move");
        Check(Convert.ToInt32(byName.Value) == 1234, "while the node does");
    }


    // =====================================================================
    // 3. Descendants -- the child tree
    // =====================================================================
    //
    // Declare creates the child collection on demand, so building a tree is
    // just a chain of declares -- and every interior node is itself an ordinary
    // node, which is what makes the model uniform. There is no separate "node"
    // type to learn here either.
    //
    static void Demo_Descendants (Store store)
    {
        Section("3. Descendants -- building a tree");

        var order = store.Root.Declare("Order", 0, true);
        order.Declare("OrderId",  10045, true);
        order.Declare("Customer", "Ivyware Pty Ltd", true);
        order.Declare("Total",    1299.50, true);

        Check(order.Exists("OrderId"), "OrderId is there");
        Check(order.Exists("Customer"), "Customer is there");
        Check(!order.Exists("Missing"), "and a name that was never declared is not");
        Check(Convert.ToInt32(order.Child("OrderId").Value) == 10045, "OrderId reads back");
        Check((string)order.Child("Customer").Value == "Ivyware Pty Ltd", "Customer reads back");
        Check((double)order.Child("Total").Value == 1299.50, "Total reads back");

        // Child is LIVE, so a write through it lands in the tree. (Item, in
        // section 2, is the one that does not.)
        order.Child("Total").Value = 1350.00;
        Check((double)order.Child("Total").Value == 1350.00, "a write through Child lands");

        // A missing child is a NAMED error, not an empty object -- so a typo is
        // visible at the call that made it.
        IMsgFieldCom missing;
        int hr = Api.Call(() => order.Child("Totl"), out missing);
        Check(hr == Hr.msgcNoField, "a typo is msgcNoField at the call that made it");
        ShowError("Child(\"Totl\")", hr);

        // Nesting: declare into the child you just declared.
        var ship = order.Declare("ShipTo", "", true);
        ship.Declare("City",     "Melbourne", true);
        ship.Declare("Postcode", 3000, true);
        Check(order.Child("ShipTo").Exists("City"), "two levels down");
        Check((string)order.Child("ShipTo").Child("City").Value == "Melbourne", "reads back");
        Check(Convert.ToInt32(order.Child("ShipTo").Child("Postcode").Value) == 3000, "and so does its sibling");

        // ... or reach it in one call by path, which is what a config file wants.
        Check((string)store.Com.FieldAt(".Order.ShipTo.City").Value == "Melbourne",
              "FieldAt walks the whole path");

        // Child and Item take ONE LITERAL NAME and never parse, so a dotted
        // string is a name containing dots to them. FieldAt is the one that
        // walks a path. Worth pinning: it is the mistake every caller makes once.
        hr = Api.Call(() => order.Child("ShipTo.City"), out missing);
        Check(hr == Hr.msgcNoField, "Child never parses a path");

        Check(order.Count == 4, "four children");
        Check(order.Delete("Total"), "Delete reports it removed one");
        Check(!order.Exists("Total"), "and it is gone");
        Check(order.Count == 3, "count followed");
        Check(!order.Delete("Total"), "deleting it again is False, not an error");

        Note("Order has {0} children, ShipTo.City='{1}'",
             order.Count, store.Com.FieldAt(".Order.ShipTo.City").Value);

        // update:true re-declares in place instead of adding a duplicate name.
        order.Declare("OrderId", 20090, true);
        Check(Convert.ToInt32(order.Child("OrderId").Value) == 20090, "update:true overwrote");
        Check(order.Count == 3, "without adding a duplicate");

        // update:false now SAYS the name was taken, where it used to succeed and
        // answer the existing child unchanged. Deliberate, one layer down, and
        // the better contract: a silent "here is the one that was already there"
        // makes CREATE and ASSIGN indistinguishable at the call site.
        object taken;
        Check(Api.Call(() => order.Declare("OrderId", 111, false), out taken) == Hr.msgcDeclare,
              "update:false onto a name that exists is msgcDeclare");
        Check(Convert.ToInt32(order.Child("OrderId").Value) == 20090, "and left it alone");
        Check(order.Count == 3, "and added nothing");

        // A defaults pass is still one call, spelled the way it means.
        if (!order.Exists("Currency")) order.Declare("Currency", "AUD", false);
        Check((string)order.Child("Currency").Value == "AUD", "ask, then declare what is missing");
        Check(order.Delete("Currency"), "tidied away again");

        // The descendant collection reached as a collection -- the same children
        // under another name.
        //
        // ITS Item IS LIVE TOO NOW. It used to be a detached copy -- there was no
        // live accessor for a collection member at all -- so reading was the only
        // safe thing to do with one and Declare was the only write path.
        var desc = order.Descendants(true);
        Check(desc.Count == 3, "the collection agrees about the count");
        Check(desc.Exists("OrderId"), "and about the names");
        Check(desc.Item("OrderId").Path == ".Order.OrderId", "a member knows its path");
        desc.Item("OrderId").Value = 10046;
        Check(Convert.ToInt32(order.Child("OrderId").Value) == 10046, "and a write through it lands");
        Check(Convert.ToInt32(store.Com.FieldAt(desc.Item("OrderId").Path).Value) == 10046,
              "which the path reaches too");
        desc.Item("OrderId").Value = 20090;

        // Truncate empties the whole child collection.
        var scratch = store.Root.Declare("Scratch", 0, true);
        scratch.Declare("a", 1, true);
        scratch.Declare("b", 2, true);
        Check(scratch.Count == 2, "two children before");
        scratch.Truncate();
        Check(scratch.Count == 0, "none after Truncate");

        // Move and retype, the two refactors the flat FileSystem layer needs
        // and that this tier inherits for free.
        var bin = store.Root.Declare("Archive", 0, true);
        Check(order.MoveChild(bin, "Customer"), "MoveChild reports it moved one");
        Check(!order.Exists("Customer"), "gone from the source");
        Check(bin.Exists("Customer"), "and present at the destination");
        Check((string)bin.Child("Customer").Value == "Ivyware Pty Ltd", "with its value");

        // RetypeChild takes a TYPE, not a value. It used to take a VARIANT,
        // because the retype entry points below were one per type and a value
        // was the only way to pick one -- but a retype cannot keep the old value
        // by definition, so that argument was always a fiction: you passed
        // "twenty thousand and ninety" and the node was left holding an empty
        // string. It seeds a ZERO of the new type, and now says so.
        Check(order.Child("OrderId").TypeName == "INT32", "OrderId starts as INT32");
        Check(order.RetypeChild("OrderId", MsgDataType.WStr), "RetypeChild reports it");
        Check(order.Child("OrderId").TypeName == "WSTR16", "and the type changed");
        Check((string)order.Child("OrderId").Value == "", "seeded with a zero of the new type");
        order.Child("OrderId").Value = "twenty thousand and ninety";
        Check((string)order.Child("OrderId").Value == "twenty thousand and ninety",
              "and the value is written afterwards, deliberately");

        // An unrecognised type is msgcType rather than a silent fallback to
        // INT32. 99 and not 999: a MsgDataType is one byte, so anything outside
        // 0..255 is not an unrecognised TYPE, it is a bad ARGUMENT -- and the
        // CLR rewrites E_INVALIDARG into ArgumentException before a caller sees
        // it, which is a finding this tree already records elsewhere.
        Check(Api.Call(() => order.RetypeChild("OrderId", 99)) == Hr.msgcType,
              "an unrecognised type is msgcType");

        // MoveChild to a node of ANOTHER store is msgcForeign, and the reason
        // is worth knowing: a node reference here is a PATH, so a foreign
        // destination would otherwise resolve that path in THIS store and move
        // the child somewhere plausible and wrong.
        int hr2;
        using (var other = Store.Create(out hr2))
        {
            Check(other != null, "a second store, for the foreign check");
            if (other != null)
            {
                var elsewhere = other.Root.Declare("Elsewhere", 0, true);
                hr = Api.Call(() => order.MoveChild(elsewhere, "OrderId"));
                Check(hr == Hr.msgcForeign, "a destination in another store is msgcForeign");
                ShowError("MoveChild to another store", hr);
                Check(order.Exists("OrderId"), "and nothing moved");
            }
        }
    }


    // =====================================================================
    // 4. Attributes -- the parallel tree
    // =====================================================================
    //
    // A SECOND child collection hanging off the same node, addressed
    // separately. Use it for metadata that should not appear when something
    // walks the content tree: units, permissions, provenance, a cache marker.
    // The TreeFs layer surfaces this collection as ".attr/" and as POSIX
    // xattrs, precisely because it is out-of-band from the data.
    //
    // THE `create` FLAG IS NOW ACCEPTED AND IGNORED. It used to be the flat
    // ABI's bCreate showing through: Attributes(create:false) on a node with no
    // attributes answered msgcNoColl. There is no state for the flag to select
    // any more -- a collection comes into being when the first thing is declared
    // into it, one layer down -- so either way a caller gets an empty
    // collection, and IsEmpty is the question that was really being asked.
    //
    static void Demo_Attributes (Store store)
    {
        Section("4. Attributes -- metadata beside the value");

        var temp = store.Root.Declare("Temperature", 21.5, true);
        Check(!temp.IsAttributed, "a fresh node carries no attribute collection");

        IMsgAttrCom none;
        int hr = Api.Call(() => temp.Attributes(false), out none);
        Check(hr == Hr.S_OK, "asking without create answers an empty collection");
        Check(none != null && none.IsEmpty, "which is empty, and says so");
        Check(none != null && none.Count == 0, "with a count of zero");

        var attr = temp.Attributes(true);
        attr.Declare("Unit",   "Celsius", true);
        attr.Declare("Sensor", 7, true);

        Check(store.Root.Child("Temperature").IsAttributed, "now it is attributed");
        Check(attr.Exists("Unit"), "Unit is there");
        Check(attr.Exists("Sensor"), "Sensor is there");
        Check(!attr.Exists("Nobody"), "and nothing else is");
        Check(attr.Count == 2, "two attributes");
        Check(!attr.IsEmpty, "so not empty");

        Check((string)attr.Item("Unit").Value == "Celsius", "Unit reads back");
        Check(Convert.ToInt32(attr.Item("Sensor").Value) == 7, "Sensor reads back");

        // The value itself is untouched by any of that.
        Check((double)store.Root.Child("Temperature").Value == 21.5, "the node's own value is untouched");

        // Attributes and descendants are independent collections on one node.
        store.Root.Child("Temperature").Declare("Reading", 21.5, true);
        Check(store.Root.Child("Temperature").IsDescendant, "now it has descendants too");
        Check(store.Root.Child("Temperature").Count == 1, "one descendant");
        Check(attr.Count == 2, "and still two attributes -- separate trees");

        Note("Temperature={0}  @Unit='{1}'  @Sensor={2}",
             store.Root.Child("Temperature").Value,
             attr.Item("Unit").Value, attr.Item("Sensor").Value);

        // AN ATTRIBUTE NOW HAS A PATH, which is the other half of what the '@'
        // separator bought. This used to be a documented HOLE: the old spelling
        // was a dotted chain of descendant names with no way to say "attribute",
        // so an attribute answered an EMPTY path and (Path empty, IsLive False)
        // was the report -- a report a caller could do nothing with.
        var unit = attr.Item("Unit");
        Check(unit.Path == ".Temperature@Unit", "an attribute has a path, with '@'");
        Check((string)store.Com.FieldAt(".Temperature@Unit").Value == "Celsius",
              "and FieldAt reaches it");

        // The root is still the one node with an empty path -- and now that is
        // the only thing an empty path means.
        Check(store.Root.Path.Length == 0, "the root, and only the root, has no path");

        // It is still not on the descendant chain, which is the whole point of a
        // second collection.
        Check(!store.Root.Child("Temperature").Exists("Unit"), "Child cannot see an attribute");

        // Declare on the collection is a write path and reaches the store.
        attr.Declare("Unit", "Kelvin", true);
        var reread = store.Root.Child("Temperature").Attributes(false);
        Check((string)reread.Item("Unit").Value == "Kelvin", "a write through the collection landed");

        Check(attr.Delete("Sensor"), "Delete removed one");
        Check(attr.Count == 1, "count followed");
        attr.Truncate();
        Check(attr.Count == 0, "Truncate emptied it");
        Check(attr.IsEmpty, "and IsEmpty agrees");
    }


    // =====================================================================
    // 5. The shape predicates -- and the stack, now four members of a node
    // =====================================================================
    //
    // THIS SECTION HAS BEEN REWRITTEN TWICE. It first said the per-node saved
    // value could not be reached from the COM tier at all; then it reached it
    // through IMsgStackCom, a whole coclass wrapping the core's MsgStck. Neither
    // is the case now. What that object was is ONE saved (name, value) pair
    // living INSIDE the node, so it is four members of IMsgFieldCom -- no second
    // object, no second lifetime to get wrong, and Pop answering whether it
    // actually restored anything.
    //
    // TWO OF THE SEVEN PREDICATES WENT WITH IT: IsAttr and IsDesc asked "is this
    // node ITSELF a collection object", and a collection is a SCOPE of a node
    // now, not a thing a node can be.
    //
    static void Demo_Shapes (Store store)
    {
        Section("5. Shape predicates -- and the stack, now four members of a node");

        var f = store.Root.Declare("Stackable", "Data", true);

        // All five are readable and all five are False for a plain node, which
        // is exactly what makes them useful: they classify a node loaded from a
        // file that this process did not build.
        Check(!f.IsList, "not a list");
        Check(!f.IsVect, "not a vector");
        Check(!f.IsStacked, "nothing stacked yet");
        Check(!f.IsAttributed, "carries no attributes");
        Check(!f.IsDescendant, "carries no descendants");

        // The stack. The NAME travels with the value, which is the thing a
        // hand-rolled `var saved = f.Value` cannot do.
        f.PushValue();
        Check(f.IsStacked, "PushValue saved the pair");

        f.Value = "speculative";
        Check((string)f.Value == "speculative", "the node can then be overwritten");

        Check(f.PopValue(), "PopValue ANSWERS whether it restored anything");
        Check(!f.IsStacked, "nothing stacked afterwards");
        Check((string)f.Value == "Data", "and the value came back");

        // POP'S ANSWER IS THE LOOP CONDITION, which the old object's was not: it
        // was a silent no-op on an empty stack and still returned success, so a
        // `do { Pop(); } while (ok)` drain loop never terminated.
        Check(!f.PopValue(), "a second pop answers False rather than failing");
        Check(Api.Call(() => f.PopValue()) == Hr.S_OK, "the CALL still succeeds -- it is not an error");

        // Drop forgets the saved pair instead of restoring it.
        f.PushValue();
        f.Value = "kept";
        Check(f.DropValue(), "DropValue reports it dropped one");
        Check(!f.IsStacked, "nothing stacked");
        Check((string)f.Value == "kept", "and the value was NOT rolled back");
        Check(!f.DropValue(), "dropping nothing answers False");

        // AND IT NESTS -- push, push, pop, pop unwinds. Every tier here
        // documented the opposite until a client pushed twice: the kernel merely
        // ASSERTED that the saved slot was empty while implementing a linked
        // stack, so a second push tripped an assertion in a debug build and the
        // documentation was written from the assertion.
        f.Value = "first";
        f.PushValue(); f.Value = "second";
        f.PushValue(); f.Value = "third";
        Check(f.PopValue() && (string)f.Value == "second", "one pop unwinds one level");
        Check(f.IsStacked, "with one still below it");
        Check(f.PopValue() && (string)f.Value == "first", "and the next unwinds that one");
        Check(!f.IsStacked, "leaving nothing stacked");

        // The saved pair lives in the STORE, not in this object, so any
        // reference to that node can pop what another one pushed.
        var other = store.Root.Child("Stackable");
        f.PushValue();
        Check(other.IsStacked, "a second reference sees the saved pair");
        other.Value = "via the other handle";
        Check(other.PopValue(), "and can pop it");
        Check((string)f.Value == "first", "which the first reference sees");

        Note("IsStacked is the state; PushValue/PopValue/DropValue are the verbs,");
        Note("and they live on the NODE -- see RecursTimeTest section 7.");
    }


    // =====================================================================
    // 6. The error channel
    // =====================================================================
    //
    // Msgcore does not return error codes and does not throw std::exception: it
    // throws a POINTER to a P2Pevent carrying module, message, group and
    // advice, and a caller that catches one must dispose of it.
    //
    // NONE OF THAT CROSSES A COM VTABLE, so MsgcoreCom catches at every entry
    // point and converts to an HRESULT plus an IErrorInfo. This tree is one
    // conversion further along again, and THAT step is the one with a trap in
    // it:
    //
    //   Msgcore   throw P2Pevent*                (module, group, advice, site)
    //   COM       HRESULT + IErrorInfo           (a code, and one sentence)
    //   CLR       an exception object            (...of a type the CLR CHOSE)
    //
    // The CLR does not simply wrap the HRESULT. It rewrites a fixed table of
    // well-known codes into CLR exception types first, so `catch (COMException)`
    // is correct for Msgcore's own 0x8004030x range and WRONG for the argument
    // validation the IDL added for automation clients. Both cases are measured
    // below; Api.Call uses Marshal.GetHRForException, which is correct for both.
    //
    static void Demo_ErrorChannel (Store store)
    {
        Section("6. The error channel -- an HRESULT, then whatever the CLR made of it");

        var host = store.Root.Declare("Names", 0, true);
        host.Declare("keep", 1, true);
        Check(host.Count == 1, "one child to start");

        // 63 UTF-16 units is in bounds; 64 is one over. Where the C++ API
        // throws before writing, this one answers msgcName before calling
        // Msgcore at all -- so the failure is cheaper AND the store is provably
        // untouched.
        string str63 = new string('a', 63);
        host.Declare(str63, 1, true);
        Check(host.Exists(str63), "63 units is in bounds");

        string str64 = new string('a', 64);
        int hr = Api.Call(() => host.Declare(str64, 1, true));
        Check(hr == Hr.msgcName, "64 is refused with msgcName");
        ShowError("Declare(64 characters)", hr);

        hr = Api.Call(() => host.Declare("", 1, true));
        Check(hr == Hr.msgcName, "and so is an empty name");

        // THE BOUND COUNTS UTF-16 UNITS, NOT CODE POINTS: one astral code point
        // costs two. 31 rockets is 62 units and fits; 32 is 64 and does not.
        //
        // This is the same fact the C++ harness pins with SysStringLen, and it
        // is worth pinning in C# because String.Length is ALSO a count of
        // UTF-16 units -- so the CLR, the BSTR and the WSTR16 cell all agree
        // about the length of this string by construction rather than by luck.
        string str31 = string.Concat(System.Linq.Enumerable.Repeat("\U0001F680", 31));
        Check(str31.Length == 62, "31 astral code points are 62 UTF-16 units in C# too");
        host.Declare(str31, 1, true);
        Check(host.Exists(str31), "and 62 units fits");

        string str32 = str31 + "\U0001F680";
        Check(str32.Length == 64, "32 of them are 64");
        hr = Api.Call(() => host.Declare(str32, 1, true));
        Check(hr == Hr.msgcName, "which does not fit");

        // The rejected writes left the collection exactly as it was.
        Check(host.Count == 3, "three children: keep, the 63-a name, the 31-rocket name");
        Check(host.Exists("keep"), "and the first one is still there");

        // A VALUE with no representation is refused the same way, and the
        // sentence says what WOULD be accepted -- the difference between an
        // error a caller can act on and one they have to go and research.
        hr = Api.Call(() => host.Declare("nothing", null, true));
        Check(hr == Hr.msgcType, "a value with no representation is msgcType");
        ShowError("Declare(name, null)", hr);
        Check(!host.Exists("nothing"), "and nothing was created");

        // --- the CLR's rewriting, measured -------------------------------------
        //
        // Msgcore's OWN codes arrive as COMException and carry the server's
        // sentence, because they are not in the CLR's table.
        Exception caught = null;
        try { host.Declare("nothing", null, true); }
        catch (Exception e) { caught = e; }
        Check(caught is COMException, "msgcType arrives as a COMException...");
        Check(Marshal.GetHRForException(caught) == Hr.msgcType, "...carrying the code unchanged");
        Note("msgcType  -> {0}: {1}", caught.GetType().Name,
             caught.Message.Split('\n')[0]);

        // E_INVALIDARG DOES NOT. DeclareTyped range-checks its dataType and
        // answers E_INVALIDARG, and the CLR turns that into an
        // ArgumentException before the caller sees it -- so a client that wrote
        // `catch (COMException)` around this API crashes here, on the one
        // validation an automation client is most likely to trip.
        caught = null;
        try { host.DeclareTyped("bad", 1, 999, true); }
        catch (Exception e) { caught = e; }
        Check(caught != null, "an out-of-range dataType fails");
        Check(!(caught is COMException), "and NOT as a COMException -- the CLR rewrote it");
        Check(caught is ArgumentException, "into an ArgumentException");
        Check(Marshal.GetHRForException(caught) == Hr.E_INVALIDARG,
              "with the original HRESULT still recoverable");
        Note("E_INVALIDARG -> {0} (this is the trap; Api.Call catches Exception for it)",
             caught.GetType().Name);

        // Reading a closed store is the last code worth branching on. Note the
        // ORDER: everything derived from a store keeps it alive, so this is a
        // clean error and not a use-after-free, however careless the release
        // order is.
        int hrCreate;
        var doomed = Store.Create(out hrCreate);
        Check(doomed != null, "a store to close");
        var itsRoot = doomed.Root;
        itsRoot.Declare("x", 1, true);
        doomed.Com.Close();
        Check(!doomed.Com.IsValid, "closed");

        object v;
        hr = Api.Call(() => itsRoot.Value, out v);
        Check(hr == Hr.msgcClosed, "a node of a closed store is msgcClosed");
        ShowError("a node of a closed store", hr);
        Check(Api.Call(() => doomed.Com.Close()) == Hr.S_OK, "Close is idempotent");
        doomed.Dispose();

        // Clear is the other end of the same story: the store EMPTIES and stays
        // usable, KEEPS ITS FILENAME, and every node reference a client holds
        // stays valid and reports msgcStale -- which is what "the store was
        // emptied" should look like from a handle. It is new: the old Nullify
        // sat over a core call that closes the heap and leaves the manager
        // pointing at nothing while still answering is_valid.
        int hrEmpty;
        using (var emptied = Store.Create(out hrEmpty))
        {
            Check(emptied != null, "a store to empty");
            var held = emptied.Root.Declare("gone", 1, true);
            Check(emptied.Root.Count == 1, "one child before");

            emptied.Com.Clear();
            Check(emptied.Com.IsValid, "still valid after Clear");
            Check(emptied.Root.Count == 0, "and empty");

            // The ROOT is the one reference that survives, because its route is
            // empty and the new tree has a root too. A reference to anything
            // BELOW it is msgcStale -- the node it names is gone.
            object gone;
            Check(Api.Call(() => held.Value, out gone) == Hr.msgcStale,
                  "a reference held across Clear is msgcStale");

            emptied.Root.Declare("again", 2, true);
            Check(Convert.ToInt32(emptied.Root.Child("again").Value) == 2,
                  "and the store is genuinely usable, not merely valid-looking");
        }
    }


    // =====================================================================
    // main
    // =====================================================================
    [STAThread]
    static int Main ()
    {
        InitConsole();
        Console.WriteLine("=== DataFieldTestNet - the Msgcore data model from C# ===");

        int hr;
        using (var store = Store.Create(out hr))
        {
            if (store == null) return SetupFailure("new MsgcoreCom.MsgStore", hr);

            Log("MAIN", "{0}", store.Com.VersionString);

            Demo_TypedCells   (store);
            Demo_NameAndValue (store);
            Demo_Descendants  (store);
            Demo_Attributes   (store);
            Demo_Shapes       (store);
            Demo_ErrorChannel (store);
        }

        return Verdict();
    }
}
