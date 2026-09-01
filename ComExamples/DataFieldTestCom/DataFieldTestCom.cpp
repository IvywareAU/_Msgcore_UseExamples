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
// DataFieldTestCom.cpp
//
// The Msgcore DATA MODEL through COM. The counterpart of
// ..\DirectExamples\DataFieldTest, and the first thing to read here.
//
// This program links NOTHING of MSCS. No Msgcore.lib, no MFC, no ATL -- only
// ole32/oleaut32/uuid and the type library. Every object it touches is found in
// the registry at run time. That is the distinguishing property of this whole
// tree, and it is what makes the comparison with the C++ original meaningful:
// the same subject, reached across a boundary that a script, a VBA macro or a
// .NET host could also reach.
//
// TWO THINGS DO NOT CROSS THAT BOUNDARY, and this harness pins both rather than
// quietly omitting them:
//
//   * A FREE-STANDING P3PmsgData. In C++ a typed cell is an object you can hold
//     on its own; here every value lives in a NODE of a store, because there is
//     no handle for a bare cell below this layer. Section 1 therefore builds its
//     cells as children of a scratch store, which is the closest honest
//     equivalent and costs nothing.
//   * P2Pevent as a THROWN object. Section 6: here the error channel is an
//     HRESULT plus an IErrorInfo sentence, which is strictly more usable from a
//     scripting host and strictly less expressive about the throw site.
//
// TWO OTHERS USED TO BE LISTED and are not absent any more. MsgStck, the
// per-node saved value, is four members of IMsgFieldCom (section 5;
// RecursTimeTestCom section 7 uses it properly). And the LIVE / DETACHED split
// that section 2 used to be half about is gone entirely: this server sits on
// MsgFacade now, where a node is the ROUTE to itself rather than a heap pointer,
// so every node handed out is live and there is no copy to hand back.
//
// Verdict is reported by process EXIT CODE:
//   0 = SUCCESS  1 = SETUP (server not registered)  2 = ASSERT  3 = a check failed

#include "../common/ComHarness.h"

using namespace msgc;


// =========================================================================
// 1. Typed value cells
// =========================================================================
//
// A Msgcore node is a discriminated value: a type tag plus storage sized for
// that tag. In C++ each c_XXX() accessor asserts the tag matches, and c_int()
// on a DOUBLE cell THROWS rather than truncating.
//
// Here there is one accessor, `Value`, and it is a VARIANT -- which already
// carries its own type, so the whole strict-accessor problem disappears at this
// tier. What does NOT disappear is the node's DECLARED WIDTH: a VARIANT cannot
// say "UINT08", so DeclareTyped exists for the cases where the width is data.
//
static void Demo_TypedCells ( Store& store )
{
    Section ( L"1. Typed value cells -- one VARIANT instead of sixteen accessors" );

    Field cells;
    CHECK ( SUCCEEDED ( store.root().declare ( L"cells", Var ( 0 ), &cells ) ) );

    // Integers. A VARIANT-typed declare always lands as INT32, which is the
    // flat ABI's msgcore_field_declare_int.
    CHECK ( SUCCEEDED ( cells.declare ( L"anInt", Var ( 12345 ) ) ) );
    CHECK ( cells.child ( L"anInt" ).asLong() == 12345 );
    CHECK ( cells.child ( L"anInt" ).dataType() == TYPE_INT32 );
    CHECK ( cells.child ( L"anInt" ).typeName() == L"INT32" );

    // Writing through the live child reaches the store, and PRESERVES the
    // declared type rather than re-typing from the VARIANT.
    CHECK ( SUCCEEDED ( cells.child ( L"anInt" ).setValue ( Var ( 999 ) ) ) );
    CHECK ( cells.child ( L"anInt" ).asLong() == 999 );
    CHECK ( cells.child ( L"anInt" ).dataType() == TYPE_INT32 );

    // The declared WIDTH is the one thing a VARIANT cannot express, so it has
    // its own verb. A ".value" round trip that normalised UINT08 to INT32 would
    // have rewritten the store just by reading it.
    CHECK ( SUCCEEDED ( cells.declareTyped ( L"aByte",  Var ( 200 ),  TYPE_UINT08 ) ) );
    CHECK ( SUCCEEDED ( cells.declareTyped ( L"aShort", Var ( 7 ),    TYPE_INT16 ) ) );
    CHECK ( SUCCEEDED ( cells.declareTyped ( L"aBig",   Var ( (LONGLONG)9007199254740993LL ), TYPE_INT64 ) ) );
    CHECK ( cells.child ( L"aByte"  ).typeName() == L"UINT08" );
    CHECK ( cells.child ( L"aByte"  ).asLong() == 200 );
    CHECK ( cells.child ( L"aShort" ).typeName() == L"INT16" );
    CHECK ( cells.child ( L"aShort" ).asLong() == 7 );
    // > 2^53: survives as an integer, and comes back as VT_I8 rather than being
    // squeezed into a Long -- which is why ReadFieldValue widens rather than
    // clamping.
    CHECK ( cells.child ( L"aBig" ).typeName() == L"INT64" );
    CHECK ( cells.child ( L"aBig" ).asInt64() == 9007199254740993LL );
    CHECK ( cells.child ( L"aBig" ).value().vt() == VT_I8 );

    // Doubles and booleans have their own VARIANT types, so no DeclareTyped.
    CHECK ( SUCCEEDED ( cells.declare ( L"aDouble", Var ( 2.5 ) ) ) );
    CHECK ( cells.child ( L"aDouble" ).asDouble() == 2.5 );
    CHECK ( cells.child ( L"aDouble" ).typeName() == L"DOUBLE" );

    CHECK ( SUCCEEDED ( cells.declare ( L"aBool", Var ( true ) ) ) );
    CHECK ( cells.child ( L"aBool" ).asBool() == true );
    CHECK ( cells.child ( L"aBool" ).typeName() == L"BOOL" );
    CHECK ( SUCCEEDED ( cells.child ( L"aBool" ).setValue ( Var ( false ) ) ) );
    CHECK ( cells.child ( L"aBool" ).asBool() == false );

    // Wide strings. The storage is WSTR16 -- 16-bit units, which is what the
    // wire format and the Linux port both assume -- and a BSTR is already
    // exactly that, so the string crosses this boundary with no conversion at
    // all. U+20AC below proves it.
    CHECK ( SUCCEEDED ( cells.declare ( L"aStr", Var ( L"Hello, \x20ACuro world" ) ) ) );
    CHECK ( cells.child ( L"aStr" ).asText() == L"Hello, \x20ACuro world" );
    CHECK ( cells.child ( L"aStr" ).typeName() == L"WSTR16" );
    Note ( L"wstr cell   : \"%s\"  (type=%s)",
           cells.child ( L"aStr" ).asText().c_str(),
           cells.child ( L"aStr" ).typeName().c_str() );

    // A blob is opaque bytes with a size -- no interpretation, no terminator.
    // It travels as a VARIANT-wrapped SAFEARRAY(VT_UI1), which is the one
    // automation type that means "bytes" and nothing else.
    struct Sample { int nId; double dValue; };
    Sample oSample = { 42, 3.5 };
    CHECK ( SUCCEEDED ( cells.declare ( L"aBlob", Var::bytes ( &oSample, sizeof(oSample) ) ) ) );
    CHECK ( cells.child ( L"aBlob" ).typeName() == L"BLOB16" );

    std::vector<BYTE> back = cells.child ( L"aBlob" ).asBytes();
    CHECK ( back.size() == sizeof(oSample) );
    if ( back.size() == sizeof(oSample) )
    {
        const Sample *p = (const Sample*)&back[0];
        CHECK ( p->nId == 42 && p->dValue == 3.5 );
    }

    // A GUID has no VARIANT type, so it is carried as its canonical text --
    // which is what a script compares, logs and puts in a registry key anyway.
    CHECK ( SUCCEEDED ( cells.declareTyped ( L"aGuid",
                        Var ( L"3F2504E0-4F89-11D3-9A0C-0305E82C3301" ), TYPE_GUID ) ) );
    CHECK ( cells.child ( L"aGuid" ).typeName() == L"GUID" );
    CHECK ( cells.child ( L"aGuid" ).asText() == L"3F2504E0-4F89-11D3-9A0C-0305E82C3301" );

    // FLOAT USED TO BE UNREACHABLE FROM HERE and is not any more. The flat ABI
    // had no declare for it -- msgcore_field_get_float existed, its declare did
    // not -- so this pair of checks used to assert that both FLOAT and NULL were
    // refused. DeclareTyped reaches the whole width table now.
    CHECK ( SUCCEEDED ( cells.declareTyped ( L"aFloat", Var ( 1.5 ), TYPE_FLOAT ) ) );
    CHECK ( cells.child ( L"aFloat" ).typeName() == L"FLOAT" );
    CHECK ( cells.child ( L"aFloat" ).asDouble() == 1.5 );

    // NULL is still refused, and for a reason that has not changed: it is the
    // ABSENCE of a value, so "declare one" is not a request that means anything.
    CHECK ( cells.declareTyped ( L"aNull", Var ( 0 ), TYPE_NULL ) == E_TYPE );
    ShowError ( L"DeclareTyped(NULL)", E_TYPE );

    // Text is Value SEEN AS a string, and is not a synonym: it renders a
    // numeric node with its own formatting rules.
    Note ( L"int as Text : '%s'    double as Text: '%s'",
           cells.child ( L"anInt" ).text().c_str(),
           cells.child ( L"aDouble" ).text().c_str() );
    CHECK ( !cells.child ( L"anInt" ).text().empty() );

    // The type vocabulary round-trips, so a ".type" file written from TypeName
    // reads back as the same code.
    CHECK ( store.typeFromName ( L"UINT64" ) == TYPE_UINT64 );
    CHECK ( store.typeName ( TYPE_UINT64 ) == L"UINT64" );
    CHECK ( store.typeFromName ( L"NoSuchType" ) == -1 );
}


// =========================================================================
// 2. A node is a name and a value
// =========================================================================
//
// In C++ a P3PmsgField IS a P3PmsgName and a P3PmsgData glued together by
// inheritance, which is why oField.c_int() and oField == L"Name" both work on
// it directly. Here the same node is one object with a Name property and a
// Value property, and the "no value" state shows up as IsNull rather than as an
// assertion on first write.
//
// THIS SECTION USED TO BE HALF ABOUT DETACHED COPIES: Item answered a deep copy
// with independent storage, and a write through it succeeded and reached
// nothing. That is gone -- see the note above the second half.
//
static void Demo_NameAndValue ( Store& store )
{
    Section ( L"2. A node -- a name, a value, and a reference that is a route" );

    Field people;
    CHECK ( SUCCEEDED ( store.root().declare ( L"people", Var ( 0 ), &people ) ) );

    Field johnno;
    CHECK ( SUCCEEDED ( people.declare ( L"Johnno", Var ( 42 ), &johnno ) ) );
    CHECK ( johnno.name() == L"Johnno" );
    CHECK ( johnno.asLong() == 42 );
    CHECK ( !johnno.isNull() );

    // The identity a client should KEEP is the path, not the object. It is a
    // string, it survives anything, and MsgStore.FieldAt takes it back.
    //
    // THE SPELLING CHANGED WITH THE PORT and it is worth reading once: every
    // step is INTRODUCED by its separator -- '.' for a child, '@' for an
    // attribute -- so the root is "" and its child is ".people", not "people".
    // The old dotted chain had no leading separator and no way to name an
    // attribute at all; this one is reversible, which is the whole point.
    CHECK ( johnno.path() == L".people.Johnno" );
    CHECK ( store.fieldAt ( L".people.Johnno" ).asLong() == 42 );

    // ... and a path that does not parse says so, rather than answering "no such
    // node" and leaving the caller to wonder which of the two it was.
    Field bad;
    CHECK ( store.fieldAt ( L"people.Johnno", bad ) == E_PATH );
    ShowError ( L"FieldAt with the old dotted spelling", E_PATH );

    // Renaming is done through the PARENT, because a node does not own its own
    // name -- the collection it sits in does.
    CHECK ( people.renameChild ( L"Johnno", L"Larry" ) );
    CHECK ( people.exists ( L"Larry" ) );
    CHECK ( !people.exists ( L"Johnno" ) );
    CHECK ( people.child ( L"Larry" ).asLong() == 42 );      // the value survived
    Note ( L"renamed to '%s', value still %d",
           people.child ( L"Larry" ).name().c_str(), people.child ( L"Larry" ).asLong() );

    // A HELD REFERENCE IS A PATH, NOT A POINTER, and that is visible here:
    // `johnno` named people.Johnno, which no longer exists, so it answers
    // msgcStale instead of reading a dangling heap address. Against the flat
    // ABI this is the case where a live handle has silently become garbage.
    Var v;
    CHECK ( johnno.raw()->get_Value ( v.addr() ) == E_STALE );
    ShowError ( L"the renamed-away reference", E_STALE );

    // TWO REFERENCES TO ONE NODE ARE TWO ROUTES TO ONE NODE.
    //
    // This is where Item used to be: a DETACHED deep copy with independent
    // storage, whose whole pedagogical job was to show that a write through it
    // succeeded and reached nothing -- the single most expensive mistake this
    // tier had to offer, because it had no symptom until a Save came out without
    // the change. Item, IsLive and msgcDetached are all gone, and what replaces
    // them is this: obtain the same node twice, by two different spellings, and
    // both are the real thing.
    Field byName = people.child ( L"Larry" );
    Field byPath = store.fieldAt ( L".people.Larry" );
    CHECK ( byName.ok() );
    CHECK ( byPath.ok() );
    CHECK ( byName.asLong() == 42 );
    CHECK ( byPath.asLong() == 42 );
    CHECK ( byName.dataType() == byPath.dataType() );
    CHECK ( byName.p2pos() == byPath.p2pos() );             // the same node

    // A write through EITHER is seen by the OTHER, immediately -- which is what
    // "there is no copy" means in practice.
    CHECK ( SUCCEEDED ( byName.setValue ( Var ( 99 ) ) ) );
    CHECK ( byPath.asLong() == 99 );
    CHECK ( SUCCEEDED ( byPath.setValue ( Var ( 7 ) ) ) );
    CHECK ( byName.asLong() == 7 );
    CHECK ( people.child ( L"Larry" ).asLong() == 7 );

    // A caller who WANTS an independent snapshot takes one explicitly, which is
    // the honest spelling of what Item was doing by accident.
    Var snapshot = byName.value();
    CHECK ( SUCCEEDED ( byName.setValue ( Var ( 1234 ) ) ) );
    CHECK ( snapshot.asLong() == 7 );                       // the copy is a VALUE
    CHECK ( byName.asLong() == 1234 );
}


// =========================================================================
// 3. Descendants -- the child tree
// =========================================================================
//
// Declare creates the child collection on demand, so building a tree is just a
// chain of declares -- and every interior node is itself an ordinary node,
// which is what makes the model uniform. There is no separate "node" type to
// learn here either.
//
static void Demo_Descendants ( Store& store )
{
    Section ( L"3. Descendants -- building a tree" );

    Field order;
    CHECK ( SUCCEEDED ( store.root().declare ( L"Order", Var ( 0 ), &order ) ) );

    CHECK ( SUCCEEDED ( order.declare ( L"OrderId",  Var ( 10045 ) ) ) );
    CHECK ( SUCCEEDED ( order.declare ( L"Customer", Var ( L"Ivyware Pty Ltd" ) ) ) );
    CHECK ( SUCCEEDED ( order.declare ( L"Total",    Var ( 1299.50 ) ) ) );

    CHECK ( order.exists ( L"OrderId" ) );
    CHECK ( order.exists ( L"Customer" ) );
    CHECK ( !order.exists ( L"Missing" ) );
    CHECK ( order.child ( L"OrderId" ).asLong() == 10045 );
    CHECK ( order.child ( L"Customer" ).asText() == L"Ivyware Pty Ltd" );
    CHECK ( order.child ( L"Total" ).asDouble() == 1299.50 );

    // A write through a child lands in the tree, as it does through every node
    // object here.
    CHECK ( SUCCEEDED ( order.child ( L"Total" ).setValue ( Var ( 1350.00 ) ) ) );
    CHECK ( order.child ( L"Total" ).asDouble() == 1350.00 );

    // A missing child is a NAMED error, not an empty object -- so a typo is
    // visible at the call that made it.
    Field missing;
    CHECK ( order.child ( L"Totl", missing ) == E_NO_FIELD );
    ShowError ( L"Child(\"Totl\")", E_NO_FIELD );

    // Nesting: declare into the child you just declared. Two levels down.
    Field ship;
    CHECK ( SUCCEEDED ( order.declare ( L"ShipTo", Var ( L"" ), &ship ) ) );
    CHECK ( SUCCEEDED ( ship.declare ( L"City",     Var ( L"Melbourne" ) ) ) );
    CHECK ( SUCCEEDED ( ship.declare ( L"Postcode", Var ( 3000 ) ) ) );
    CHECK ( order.child ( L"ShipTo" ).exists ( L"City" ) );
    CHECK ( order.child ( L"ShipTo" ).child ( L"City" ).asText() == L"Melbourne" );
    CHECK ( order.child ( L"ShipTo" ).child ( L"Postcode" ).asLong() == 3000 );

    // ... or reach it in one call by path, which is what a config file wants.
    CHECK ( store.fieldAt ( L".Order.ShipTo.City" ).asText() == L"Melbourne" );

    // Child takes ONE LITERAL NAME and never parses, so a dotted string is a
    // name containing dots to it -- and a dot is not a legal character in a
    // name, which is precisely what makes the path grammar reversible. FieldAt
    // is the one that walks a path. Worth pinning: it is the mistake every
    // caller makes once.
    Field byWrongRoute;
    CHECK ( FAILED ( order.child ( L"ShipTo.City", byWrongRoute ) ) );
    CHECK ( !byWrongRoute.ok() );

    CHECK ( order.count() == 4 );
    CHECK ( order.remove ( L"Total" ) );
    CHECK ( !order.exists ( L"Total" ) );
    CHECK ( order.count() == 3 );
    CHECK ( !order.remove ( L"Total" ) );                   // gone already: False, not an error

    Note ( L"Order has %d children, .Order.ShipTo.City='%s'",
           order.count(), store.fieldAt ( L".Order.ShipTo.City" ).asText().c_str() );

    // update=True re-declares in place instead of adding a duplicate name.
    CHECK ( SUCCEEDED ( order.declare ( L"OrderId", Var ( 20090 ), NULL, true ) ) );
    CHECK ( order.child ( L"OrderId" ).asLong() == 20090 );
    CHECK ( order.count() == 3 );

    // update=False now SAYS the name was taken, where it used to succeed and
    // answer the existing child unchanged. That is a deliberate change one layer
    // down, and it is the better contract: the kernel's silent "here is the one
    // that was already there" makes CREATE and ASSIGN indistinguishable at the
    // call site, so a defaults pass and a typo look identical.
    CHECK ( order.declare ( L"OrderId", Var ( 111 ), NULL, false ) == E_DECLARE );
    ShowError ( L"Declare(update:=False) onto a name that exists", E_DECLARE );
    CHECK ( order.child ( L"OrderId" ).asLong() == 20090 );  // untouched
    CHECK ( order.count() == 3 );

    // ... and a defaults pass is still one call, spelled the way it means:
    // ask, then declare only what is missing.
    CHECK ( !order.exists ( L"Currency" ) );
    if ( !order.exists ( L"Currency" ) )
        CHECK ( SUCCEEDED ( order.declare ( L"Currency", Var ( L"AUD" ), NULL, false ) ) );
    CHECK ( order.child ( L"Currency" ).asText() == L"AUD" );
    CHECK ( order.remove ( L"Currency" ) );

    // The descendant collection reached as a collection, which is the same
    // children under another name.
    //
    // ITS Item IS LIVE TOO NOW. It used to be a detached copy -- there was no
    // live accessor for a collection member at all -- so reading was the only
    // safe thing to do with one, and Declare was the only write path. Declare
    // and Item reach the same node.
    Desc desc;
    CHECK ( SUCCEEDED ( order.descendants ( desc ) ) );
    CHECK ( desc.count() == 3 );
    CHECK ( desc.exists ( L"OrderId" ) );
    CHECK ( desc.item ( L"OrderId" ).path() == L".Order.OrderId" );
    CHECK ( SUCCEEDED ( desc.item ( L"OrderId" ).setValue ( Var ( 10046 ) ) ) );
    CHECK ( order.child ( L"OrderId" ).asLong() == 10046 );  // the write landed
    CHECK ( store.fieldAt ( desc.item ( L"OrderId" ).path().c_str() ).asLong() == 10046 );
    CHECK ( SUCCEEDED ( desc.item ( L"OrderId" ).setValue ( Var ( 20090 ) ) ) );

    // Truncate empties the whole child collection.
    Field scratch;
    CHECK ( SUCCEEDED ( store.root().declare ( L"Scratch", Var ( 0 ), &scratch ) ) );
    CHECK ( SUCCEEDED ( scratch.declare ( L"a", Var ( 1 ) ) ) );
    CHECK ( SUCCEEDED ( scratch.declare ( L"b", Var ( 2 ) ) ) );
    CHECK ( scratch.count() == 2 );
    CHECK ( SUCCEEDED ( scratch.truncate() ) );
    CHECK ( scratch.count() == 0 );

    // Move and retype, the two refactors the flat FileSystem layer needs and
    // that this tier inherits for free.
    Field bin;
    CHECK ( SUCCEEDED ( store.root().declare ( L"Archive", Var ( 0 ), &bin ) ) );
    CHECK ( order.moveChild ( bin, L"Customer" ) );
    CHECK ( !order.exists ( L"Customer" ) );
    CHECK ( bin.exists ( L"Customer" ) );
    CHECK ( bin.child ( L"Customer" ).asText() == L"Ivyware Pty Ltd" );

    // RetypeChild takes a TYPE, not a value. It used to take a VARIANT, because
    // the flat ABI's retype entry points were one per type and passing a value
    // was the only way to pick one -- but a retype cannot keep the old value by
    // definition, so that argument was always a fiction: you passed
    // "twenty thousand and ninety" and the node was left holding an empty
    // string. It seeds a ZERO of the new type, and now says so.
    CHECK ( order.child ( L"OrderId" ).typeName() == L"INT32" );
    CHECK ( order.retypeChild ( L"OrderId", TYPE_WSTR ) );
    CHECK ( order.child ( L"OrderId" ).typeName() == L"WSTR16" );
    CHECK ( order.child ( L"OrderId" ).asText().empty() );
    CHECK ( SUCCEEDED ( order.child ( L"OrderId" ).setText ( L"twenty thousand and ninety" ) ) );
    CHECK ( order.child ( L"OrderId" ).asText() == L"twenty thousand and ninety" );

    // An unrecognised type is msgcType rather than a silent fallback to INT32.
    // 99 rather than 999: a MsgDataType is one byte, so anything outside 0..255
    // is not an unrecognised TYPE, it is a bad ARGUMENT, and the two get
    // different answers on purpose.
    CHECK ( order.retypeChildHr ( L"OrderId", 99 ) == E_TYPE );
    ShowError ( L"RetypeChild(99)", E_TYPE );
    CHECK ( order.retypeChildHr ( L"OrderId", 999 ) == E_INVALIDARG );
}


// =========================================================================
// 4. Attributes -- the parallel tree
// =========================================================================
//
// A SECOND child collection hanging off the same node, addressed separately.
// Use it for metadata that should not appear when something walks the content
// tree: units, permissions, provenance, a cache marker. The TreeFs layer
// surfaces this collection as ".attr/" and as POSIX xattrs, precisely because
// it is out-of-band from the data.
//
// THE `create` FLAG IS NOW ACCEPTED AND IGNORED, and the IDL says so. It used
// to be the flat ABI's bCreate showing through: Attributes(create:=False) on a
// node with no attributes answered msgcNoColl. There is no state for the flag
// to select any more -- a collection comes into being when the first thing is
// declared into it, one layer down -- so what a caller gets either way is an
// empty collection, and IsEmpty is the question that was really being asked.
// The flag stays in the signature because every existing client passes it.
//
static void Demo_Attributes ( Store& store )
{
    Section ( L"4. Attributes -- metadata beside the value" );

    Field temp;
    CHECK ( SUCCEEDED ( store.root().declare ( L"Temperature", Var ( 21.5 ), &temp ) ) );
    CHECK ( !temp.isAttributed() );

    Attr none;
    CHECK ( SUCCEEDED ( temp.attributes ( none, false ) ) );
    CHECK ( none.ok() );
    CHECK ( none.isEmpty() );
    CHECK ( none.count() == 0 );

    Attr attr;
    CHECK ( SUCCEEDED ( temp.attributes ( attr, true ) ) );
    CHECK ( SUCCEEDED ( attr.declare ( L"Unit",   Var ( L"Celsius" ) ) ) );
    CHECK ( SUCCEEDED ( attr.declare ( L"Sensor", Var ( 7 ) ) ) );

    CHECK ( store.root().child ( L"Temperature" ).isAttributed() );
    CHECK ( attr.exists ( L"Unit" ) );
    CHECK ( attr.exists ( L"Sensor" ) );
    CHECK ( !attr.exists ( L"Nobody" ) );
    CHECK ( attr.count() == 2 );
    CHECK ( !attr.isEmpty() );

    CHECK ( attr.item ( L"Unit" ).asText() == L"Celsius" );
    CHECK ( attr.item ( L"Sensor" ).asLong() == 7 );

    // The value itself is untouched by any of that.
    CHECK ( store.root().child ( L"Temperature" ).asDouble() == 21.5 );

    // Attributes and descendants are independent collections on one node.
    CHECK ( SUCCEEDED ( store.root().child ( L"Temperature" ).declare ( L"Reading", Var ( 21.5 ) ) ) );
    CHECK ( store.root().child ( L"Temperature" ).isDescendant() );
    CHECK ( store.root().child ( L"Temperature" ).count() == 1 );
    CHECK ( attr.count() == 2 );                            // still 2 -- separate trees

    Note ( L"Temperature=%.1f  @Unit='%s'  @Sensor=%d",
           store.root().child ( L"Temperature" ).asDouble(),
           attr.item ( L"Unit" ).asText().c_str(),
           attr.item ( L"Sensor" ).asLong() );

    // AN ATTRIBUTE NOW HAS A PATH, which is the other half of what the '@'
    // separator bought. This used to be a documented HOLE: the old path spelling
    // was a dotted chain of descendant names with no way to say "attribute", so
    // an attribute answered an EMPTY path and the pair (Path empty, IsLive
    // False) was the report -- a report the caller could do nothing with.
    Field unit = attr.item ( L"Unit" );
    CHECK ( unit.path() == L".Temperature@Unit" );
    CHECK ( store.fieldAt ( L".Temperature@Unit" ).asText() == L"Celsius" );

    // The root is still the one node with an empty path, and now that is the
    // ONLY thing an empty path means.
    CHECK ( store.root().path().empty() );

    // It is still not on the descendant chain, which is the whole point of a
    // second collection: Child does not find it.
    CHECK ( !store.root().child ( L"Temperature" ).exists ( L"Unit" ) );

    // Declare on the collection IS a write path and reaches the store.
    CHECK ( SUCCEEDED ( attr.declare ( L"Unit", Var ( L"Kelvin" ) ) ) );
    Attr reread;
    CHECK ( SUCCEEDED ( store.root().child ( L"Temperature" ).attributes ( reread, false ) ) );
    CHECK ( reread.item ( L"Unit" ).asText() == L"Kelvin" );

    CHECK ( attr.remove ( L"Sensor" ) );
    CHECK ( attr.count() == 1 );
    CHECK ( SUCCEEDED ( attr.truncate() ) );
    CHECK ( attr.count() == 0 );
    CHECK ( attr.isEmpty() );
}


// =========================================================================
// 5. The shape predicates -- and MsgStck, which used to be absent
// =========================================================================
//
// The C++ model gives every field a private saved slot: Push() saves the field's
// current name and value and Pop() restores them, which is the "try something,
// put it back" idiom without the caller keeping its own stack.
//
// THIS SECTION HAS BEEN REWRITTEN TWICE. It first said the stack could not be
// reached at all; then it reached it through IMsgStackCom, a whole coclass
// wrapping the core's MsgStck. Neither is the case now. What that object was is
// ONE saved (name, value) pair living INSIDE the node, so it is four members of
// IMsgFieldCom -- no second object, no second lifetime to get wrong, and Pop
// answering whether it actually restored anything.
//
// TWO OF THE SEVEN PREDICATES WENT WITH IT. IsAttr and IsDesc asked "is this
// node ITSELF a collection object", and nothing answers to that any more: a
// collection is a SCOPE of a node, not a thing a node can be.
//
// AND IT NESTS, which every tier here got wrong until this harness pushed twice
// -- see the block below.
//
static void Demo_Shapes ( Store& store )
{
    Section ( L"5. Shape predicates -- and the stack, now four members of a node" );

    Field f;
    CHECK ( SUCCEEDED ( store.root().declare ( L"Stackable", Var ( L"Data" ), &f ) ) );

    // All five are readable, and all five are False for a plain node -- which is
    // exactly what makes them useful: they classify a node loaded from a file
    // that this process did not build.
    CHECK ( !f.isList() );
    CHECK ( !f.isVect() );
    CHECK ( !f.isStacked() );
    CHECK ( !f.isAttributed() );
    CHECK ( !f.isDescendant() );

    // The stack itself. The NAME travels with the value, which is the thing a
    // hand-rolled `Var saved = f.value()` cannot do.
    CHECK ( SUCCEEDED ( f.pushValue() ) );
    CHECK ( f.isStacked() );

    CHECK ( SUCCEEDED ( f.setValue ( Var ( L"speculative" ) ) ) );
    CHECK ( f.asText() == L"speculative" );

    CHECK ( f.popValue() );                                 // True: it restored
    CHECK ( !f.isStacked() );
    CHECK ( f.asText() == L"Data" );

    // POP ANSWERS WHETHER IT DID ANYTHING, which the old object's Pop did not:
    // it was a silent no-op on an empty stack and still returned success, so a
    // `do { Pop(); } while (SUCCEEDED(...))` drain loop never terminated. The
    // answer is the loop condition now.
    CHECK ( !f.popValue() );                                // nothing stacked
    // The CALL still succeeds -- "there was nothing to restore" is not a
    // failure -- so the boolean is the answer and SUCCEEDED() is not.
    CHECK ( f.popValueHr() == S_OK );

    // Drop forgets the saved pair instead of restoring it.
    CHECK ( SUCCEEDED ( f.pushValue() ) );
    CHECK ( SUCCEEDED ( f.setValue ( Var ( L"kept" ) ) ) );
    CHECK ( f.dropValue() );
    CHECK ( !f.isStacked() );
    CHECK ( f.asText() == L"kept" );                        // NOT rolled back
    CHECK ( !f.dropValue() );

    // IT NESTS, and this is the check that established it. The kernel LOOKS as
    // though it does not: MsgStck__AllocItem asserted that the saved slot was
    // empty, so a second push tripped an assertion in a debug build and this
    // tier's documentation said "pushing twice replaces the first". It was the
    // ASSERTION that was the leftover -- MsgStck::Push reads the current head,
    // allocates, and re-links the old head onto the new item, which is a linked
    // stack. Measured here and in Release, where nothing asserts: it unwinds.
    CHECK ( SUCCEEDED ( f.setValue ( Var ( L"first" ) ) ) );
    CHECK ( SUCCEEDED ( f.pushValue() ) );
    CHECK ( SUCCEEDED ( f.setValue ( Var ( L"second" ) ) ) );
    CHECK ( SUCCEEDED ( f.pushValue() ) );
    CHECK ( SUCCEEDED ( f.setValue ( Var ( L"third" ) ) ) );

    CHECK ( f.popValue() );
    CHECK ( f.asText() == L"second" );
    CHECK ( f.isStacked() );                                // still one below
    CHECK ( f.popValue() );
    CHECK ( f.asText() == L"first" );
    CHECK ( !f.isStacked() );
    CHECK ( !f.popValue() );

    // The saved pair lives in the STORE, not in this object -- so any reference
    // to that node can pop what another one pushed.
    Field other = store.root().child ( L"Stackable" );
    CHECK ( SUCCEEDED ( f.pushValue() ) );
    CHECK ( other.isStacked() );
    CHECK ( SUCCEEDED ( other.setValue ( Var ( L"via the other handle" ) ) ) );
    CHECK ( other.popValue() );
    CHECK ( f.asText() == L"first" );
    CHECK ( !f.isStacked() );

    Note ( L"IsStacked is the state; PushValue/PopValue/DropValue are the verbs," );
    Note ( L"and they live on the NODE -- see RecursTimeTestCom section 7." );
}


// =========================================================================
// 6. The error channel
// =========================================================================
//
// Msgcore does not return error codes and does not throw std::exception: it
// throws a POINTER to a P2Pevent carrying module, message, group and advice,
// and a caller that catches one must dispose of it.
//
// NONE OF THAT CROSSES A COM VTABLE -- an exception escaping one is undefined
// behaviour -- so MsgcoreCom catches at every entry point and converts. What
// arrives here is an HRESULT plus an IErrorInfo, and the trade is worth stating
// plainly:
//
//   LOST : the throw site, the module, the group, the advice, and the ability
//          to let an exception unwind several frames of the caller's own code.
//   GAINED : a failure a script can handle (VBScript's Err.Description, .NET's
//          COMException.Message), a code that can be BRANCHED on, and a process
//          that survives a defect in the store.
//
// The name bound exercised below is a real, load-bearing limit: a Msgcore node
// name is 63 UTF-16 UNITS. Where the C++ API throws before writing, this one
// answers msgcName before calling Msgcore at all -- so the failure is cheaper
// AND the store is provably untouched.
//
static void Demo_ErrorChannel ( Store& store )
{
    Section ( L"6. The error channel -- HRESULT + IErrorInfo, not a thrown P2Pevent" );

    Field host;
    CHECK ( SUCCEEDED ( store.root().declare ( L"Names", Var ( 0 ), &host ) ) );
    CHECK ( SUCCEEDED ( host.declare ( L"keep", Var ( 1 ) ) ) );
    CHECK ( host.count() == 1 );

    // 63 units is in bounds.
    std::wstring str63 ( 63, L'a' );
    CHECK ( SUCCEEDED ( host.declare ( str63.c_str(), Var ( 1 ) ) ) );
    CHECK ( host.exists ( str63.c_str() ) );

    // 64 is one over, and is refused.
    std::wstring str64 ( 64, L'a' );
    CHECK ( host.declare ( str64.c_str(), Var ( 1 ) ) == E_NAME );
    ShowError ( L"Declare(64 characters)", E_NAME );

    // An empty name is the other end of the same rule.
    CHECK ( host.declare ( L"", Var ( 1 ) ) == E_NAME );

    // THE BOUND COUNTS UTF-16 UNITS, NOT CODE POINTS: one astral code point
    // costs two. 31 rockets is 62 units and fits; 32 is 64 and does not. This
    // is the same fact the C++ harness pins with P3PmsgName::c_size(), and it
    // is worth pinning twice because a BSTR's length is in units too -- so the
    // two tiers agree by construction rather than by luck.
    std::wstring str31;
    for ( int i = 0; i < 31; ++i ) str31 += L"\U0001F680";
    CHECK ( ::SysStringLen ( Bstr ( str31.c_str() ) ) == 62 );
    CHECK ( SUCCEEDED ( host.declare ( str31.c_str(), Var ( 1 ) ) ) );

    std::wstring str32 = str31 + L"\U0001F680";
    CHECK ( ::SysStringLen ( Bstr ( str32.c_str() ) ) == 64 );
    CHECK ( host.declare ( str32.c_str(), Var ( 1 ) ) == E_NAME );

    // The rejected writes left the collection exactly as it was: "keep", the
    // 63-a name and the 31-rocket name, and nothing else.
    CHECK ( host.count() == 3 );
    CHECK ( host.exists ( L"keep" ) );

    // A VALUE with no representation is refused the same way, and the sentence
    // says what WOULD be accepted -- which is the difference between an error a
    // caller can act on and one they have to go and research.
    Var empty;
    CHECK ( host.declare ( L"nothing", empty ) == E_TYPE );
    ShowError ( L"Declare(name, Empty)", E_TYPE );
    CHECK ( !host.exists ( L"nothing" ) );

    // Reading a closed store is the last of the four codes worth branching on.
    // Note the ORDER: everything derived from a store keeps it alive, so this
    // is a clean error and not a use-after-free, however careless the release
    // order is.
    Store doomed;
    CHECK ( doomed.ok() );
    Field itsRoot = doomed.root();
    CHECK ( SUCCEEDED ( itsRoot.declare ( L"x", Var ( 1 ) ) ) );
    CHECK ( SUCCEEDED ( doomed.close() ) );
    CHECK ( !doomed.isValid() );

    Var v;
    CHECK ( itsRoot.raw()->get_Value ( v.addr() ) == E_CLOSED );
    ShowError ( L"a node of a closed store", E_CLOSED );
    CHECK ( doomed.close() == S_OK );                       // idempotent

    // Clear is the other end of the same story: the store EMPTIES and stays
    // usable, keeping its filename, and every node reference a client is holding
    // stays valid and reports msgcNoField. That is what "the store was emptied"
    // should look like from a handle -- and it is new. The old Nullify was
    // implemented over a core call that closes the heap and leaves the manager
    // pointing at nothing while still answering is_valid.
    Store emptied;
    CHECK ( emptied.ok() );
    Field held = emptied.root();
    CHECK ( SUCCEEDED ( held.declare ( L"gone", Var ( 1 ) ) ) );
    CHECK ( held.count() == 1 );
    CHECK ( SUCCEEDED ( emptied.clear() ) );
    CHECK ( emptied.isValid() );
    CHECK ( emptied.root().count() == 0 );
    CHECK ( SUCCEEDED ( emptied.root().declare ( L"again", Var ( 2 ) ) ) );
    CHECK ( emptied.root().child ( L"again" ).asLong() == 2 );
}


// =========================================================================
// main
// =========================================================================
int main ( )
{
    InitConsole();

    wprintf ( L"=== DataFieldTestCom - the Msgcore data model through COM ===\n" );
    fflush ( stdout );

    Apartment apt;
    if ( !apt.ok() ) return SetupFailure ( L"CoInitializeEx", E_FAIL );

    Store store;
    if ( !store.ok() ) return SetupFailure ( L"CoCreateInstance(MsgcoreCom.MsgStore)", store.hr() );

    Log ( L"MAIN", L"%s", store.versionString().c_str() );

    Demo_TypedCells   ( store );
    Demo_NameAndValue ( store );
    Demo_Descendants  ( store );
    Demo_Attributes   ( store );
    Demo_Shapes       ( store );
    Demo_ErrorChannel ( store );

    return Verdict();
}
