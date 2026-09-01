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
// MsgcoreComInterop.cs
//
// MsgcoreCom.idl, by hand.
//
// A C++ client gets IMsgStoreCom and friends from MIDL's MsgcoreCom_h.h. A .NET
// client normally gets them from TlbImp or "Add Reference", which produces an
// *interop assembly* -- a managed DLL of nothing but [ComImport] interface
// declarations. This file IS that interop assembly, written out in source so the
// tree has no generated inputs and no build step beyond csc.
//
// ---------------------------------------------------------------------------
// TWO THINGS ARE LOAD-BEARING, and both are easy to get wrong silently.
// ---------------------------------------------------------------------------
//
// 1. VTABLE ORDER. `dual` means "IUnknown, then IDispatch, then my members in
//    DECLARATION order". Every member occupies a slot, including each half of a
//    property. Get one out of order and nothing complains: the call goes to the
//    neighbouring slot with the neighbouring signature, and what happens next
//    depends on what the arguments happen to look like as the other type.
//
//    The order below is not transcribed from the .idl by eye -- it was read off
//    MIDL's own output, the `virtual HRESULT STDMETHODCALLTYPE` sequence in
//    MsgcoreCom_h.h, which is the definition of what the server's vtable is.
//    Two places where the .idl reads in one order and lays out in another:
//
//      IMsgVectCom  get_Item is slot 1 and put_Item is slot 9 -- SEVEN members
//                   apart, because the .idl appended the setter with Count. So
//                   Item CANNOT be a C# indexer here: an indexer emits get and
//                   set adjacently and would shift eight members by one. It is
//                   declared as two ordinary methods in the right two places.
//
//      IMsgListCom  get_Item / put_Item ARE adjacent (slots 7-8), so an indexer
//                   would work -- and it is still written as two methods, so
//                   that the two container interfaces read the same way and a
//                   later append to either cannot quietly break one of them.
//
// 2. MARSHALLING. Everything is annotated even where the default would do, so
//    the IDL <-> C# mapping can be read off the page:
//
//        BSTR                     ->  [MarshalAs(UnmanagedType.BStr)] string
//        VARIANT                  ->  [MarshalAs(UnmanagedType.Struct)] object
//        VARIANT_BOOL             ->  [MarshalAs(UnmanagedType.VariantBool)] bool
//        LONG                     ->  int
//        hyper                    ->  long
//        DATE                     ->  DateTime          (the CLR converts)
//        IMsgFieldCom *           ->  [MarshalAs(UnmanagedType.Interface)]
//        [out, retval] X *pVal    ->  the C# return value
//
// ---------------------------------------------------------------------------
// WHAT THIS TIER GETS THAT THE C++ ONE PAID FOR.
// ---------------------------------------------------------------------------
//
// ComExamples\common\ComHarness.h is 1356 lines, and most of
// them are there because C++ has to do by hand what the marshaller does here: a
// BSTR wrapper, a VARIANT wrapper with eight typed readers, a smart pointer, an
// IEnumVARIANT loop, and a from-scratch IDispatch implementation for the event
// sink. None of that appears in this tree. `store.Root.Child("width").Value` is
// one expression whose every step is BSTR-allocating, VARIANT-returning and
// reference-counted, and none of that is visible.
//
// THE ONE THING IT COSTS is the error channel, and it is worth understanding
// before reading any harness here. These declarations are NOT [PreserveSig], so
// a failed HRESULT is raised as an exception -- which is what a .NET consumer
// wants and what TlbImp would have produced. But the CLR does not deliver every
// failure as a COMException: it REWRITES a fixed table of well-known HRESULTs
// into CLR exception types first. Measured against this very server:
//
//     msgcDetached      0x8004030B -> COMException        (custom, passes through)
//     msgcNotList       0x80040307 -> COMException
//     DISP_E_TYPEMISMATCH          -> COMException
//     E_INVALIDARG      0x80070057 -> ArgumentException   (REWRITTEN)
//     E_POINTER         0x80004003 -> NullReferenceException
//
// So `catch (COMException)` around this API is correct for Msgcore's own range
// and wrong for the argument validation the IDL added for automation clients.
// Msgc.Call in ComHarness.cs catches Exception and uses
// Marshal.GetHRForException, which recovers the original code from whichever
// type the CLR chose. Every harness here goes through it.
//
// AND ONE MORE, which is this tier's own: a SUCCESS code other than S_OK is
// invisible. MoveChild answers S_FALSE for "already there"; Save answers S_OK.
// A `void` or valued signature has nowhere to put the difference and the
// marshaller discards it. Where that mattered the .idl reshaped the member into
// an [out, retval] VARIANT_BOOL -- MoveChild's pMoved, Delete's pDeleted --
// which is exactly why those parameters exist. Nothing in this file can
// recover a success code that was not reshaped that way.

using System;
using System.Runtime.InteropServices;

namespace MsgcoreCom
{
    // =======================================================================
    // IMsgFieldCom -- one node: a name, a typed value, and children.
    //
    // Value is DISPID_VALUE and _NewEnum is DISPID_NEWENUM, which is what makes
    // `field` read as its own value and `For Each c In field` walk the children
    // in a scripting host. Neither means anything to a vtable caller -- C# reads
    // them as Value and _NewEnum like any other member -- and both are still
    // declared, because they occupy slots.
    //
    // DECLARATION ORDER IS VTABLE ORDER, AND IT IS THE WHOLE CONTRACT. A member
    // added, removed or moved here against what the server publishes does not
    // fail to compile and does not fail to bind: it calls a DIFFERENT SLOT, with
    // the arguments of the one you wrote. Six members left this interface when
    // the server moved onto MsgFacade -- IsVoid, IsLive, Item, IsAttr, IsDesc
    // and Stack -- and four arrived, so every slot below the first change moved.
    // The dispids in the attributes are documentation for a late-bound reader;
    // they do NOT drive vtable dispatch and will not save a mismatch.
    // =======================================================================
    [ComImport]
    [Guid("C2C1C001-FB83-44FA-9AEB-C938AFCFE970")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgFieldCom
    {
        // slots 1-2: get_Value, put_Value
        object Value
        {
            [DispId(0)] [return: MarshalAs(UnmanagedType.Struct)] get;
            [DispId(0)] [param: MarshalAs(UnmanagedType.Struct)]  set;
        }

        // slot 3
        string Name { [DispId(1)] [return: MarshalAs(UnmanagedType.BStr)] get; }

        // slots 4-5. NOT a synonym for Value: reading renders a numeric node
        // with Msgcore's own formatting, writing stores a WSTR16 whatever the
        // node was.
        string Text
        {
            [DispId(2)] [return: MarshalAs(UnmanagedType.BStr)] get;
            [DispId(2)] [param: MarshalAs(UnmanagedType.BStr)]  set;
        }

        // slots 6-10.
        //
        // IsVoid (dispid 6) and IsLive (dispid 9) are GONE from the server and
        // so are gone from here. The first asked a distinction the store does
        // not draw; the second asked whether writes through this object reach
        // the store, which they now always do.
        int    DataType { [DispId(3)] get; }
        string TypeName { [DispId(4)] [return: MarshalAs(UnmanagedType.BStr)] get; }
        bool   IsNull   { [DispId(5)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        /// <summary>
        /// The path from the root, and the identity to KEEP: '.' before each
        /// child step and '@' before each attribute step, so "" is the root,
        /// ".Config" a child of it and ".Config.Window@Colour" an attribute of a
        /// grandchild. MsgStore.FieldAt takes it back unchanged -- which the old
        /// dotted chain did not, and which is why an attribute now has a path at
        /// all.
        /// </summary>
        string Path     { [DispId(7)] [return: MarshalAs(UnmanagedType.BStr)] get; }
        long   P2Pos    { [DispId(8)] get; }

        // slots 11-12
        [DispId(10)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool Exists ([MarshalAs(UnmanagedType.BStr)] string name);

        /// <summary>
        /// The one lookup. Live, like every node here -- Item, the detached deep
        /// copy that used to sit beside it at dispid 12, is gone. It reaches a
        /// LIST and a VECT too, which it could not: it resolved through
        /// SelectItem, which throws on a container.
        /// </summary>
        [DispId(11)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom Child ([MarshalAs(UnmanagedType.BStr)] string name);

        // slots 13-14. `update` has defaultvalue(-1) in the IDL; an optional
        // argument is a type-library nicety for late binding, so a vtable caller
        // passes it every time and it is declared required here.
        [DispId(13)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom Declare ([MarshalAs(UnmanagedType.BStr)]   string name,
                              [MarshalAs(UnmanagedType.Struct)] object value,
                              [MarshalAs(UnmanagedType.VariantBool)] bool update);

        /// <summary>The one thing a VARIANT cannot say: the declared WIDTH.</summary>
        [DispId(14)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom DeclareTyped ([MarshalAs(UnmanagedType.BStr)]   string name,
                                   [MarshalAs(UnmanagedType.Struct)] object value,
                                   int dataType,
                                   [MarshalAs(UnmanagedType.VariantBool)] bool update);

        // slots 15-19
        [DispId(15)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool Delete ([MarshalAs(UnmanagedType.BStr)] string name);

        [DispId(16)] void Truncate ();

        [DispId(17)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool RenameChild ([MarshalAs(UnmanagedType.BStr)] string oldName,
                          [MarshalAs(UnmanagedType.BStr)] string newName);

        /// <summary>
        /// destination must be a node OF THIS STORE -- msgcForeign otherwise,
        /// and that check is not pedantry: two stores commonly have the same
        /// shape, so a foreign destination would not fail, it would move the
        /// child into the wrong heap. Moving to where it already is answers
        /// S_FALSE and does nothing; the CLR hides S_FALSE, so the returned bool
        /// is False and that is the distinction to test.
        /// </summary>
        [DispId(18)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool MoveChild ([MarshalAs(UnmanagedType.Interface)] IMsgFieldCom destination,
                        [MarshalAs(UnmanagedType.BStr)]      string name);

        /// <summary>
        /// Takes a TYPE, not a value. It used to take a VARIANT, because the
        /// retype entry points below were one per type and a value was the only
        /// way to pick one -- but a retype seeds a ZERO of the new type by
        /// definition, so that argument was always a fiction.
        /// </summary>
        [DispId(19)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool RetypeChild ([MarshalAs(UnmanagedType.BStr)] string name, int dataType);

        // slots 20-21
        [DispId(20)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgAttrCom Attributes ([MarshalAs(UnmanagedType.VariantBool)] bool create);

        [DispId(21)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgDescCom Descendants ([MarshalAs(UnmanagedType.VariantBool)] bool create);

        // slots 22-24
        IMsgCursorCom Cursor { [DispId(22)] [return: MarshalAs(UnmanagedType.Interface)] get; }
        IMsgListCom   List   { [DispId(23)] [return: MarshalAs(UnmanagedType.Interface)] get; }
        IMsgVectCom   Vector { [DispId(24)] [return: MarshalAs(UnmanagedType.Interface)] get; }

        // slots 25-27. DATE crosses as a DateTime: the CLR does the 1899-12-30
        // epoch arithmetic, which the C++ tree has to do with VariantTimeToSystemTime.
        DateTime Timestamp { [DispId(25)] get; [DispId(25)] set; }

        [DispId(26)] void Touch ();

        // slots 28-33.
        //
        // IsAttr (30) and IsDesc (31) are GONE with the question they asked --
        // "is this node ITSELF a collection object". A collection is a SCOPE of
        // a node now, not a thing a node can be.
        int  Count        { [DispId(27)] get; }
        bool IsList       { [DispId(28)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool IsVect       { [DispId(29)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool IsStacked    { [DispId(32)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool IsAttributed { [DispId(33)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool IsDescendant { [DispId(34)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        // slots 34-37 -- the container members.
        //
        // DeclareList / DeclareVect are the ONLY way to bring a container into
        // existence from this tier. ChildList / ChildVect used to be the only
        // way to REACH one, because Child could not: it resolved through
        // SelectItem, which throws on a container -- a list is not an "item".
        // Child can now, so these two are the TYPED spellings; both answer
        // msgcNotList / msgcNotVect for a name that is present but the other
        // kind, so neither needs a pre-check.
        [DispId(35)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgListCom DeclareList ([MarshalAs(UnmanagedType.BStr)] string name);

        [DispId(36)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgVectCom DeclareVect ([MarshalAs(UnmanagedType.BStr)] string name,
                                 int count, int dataType);

        [DispId(37)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgListCom ChildList ([MarshalAs(UnmanagedType.BStr)] string name);

        [DispId(38)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgVectCom ChildVect ([MarshalAs(UnmanagedType.BStr)] string name);

        // slot 38
        IMsgRecursCom Walker { [DispId(39)] [return: MarshalAs(UnmanagedType.Interface)] get; }

        // slot 39. New: a child by POSITION, for walking without a cursor.
        [DispId(41)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom ChildAt (int index);

        // slots 40-42 -- THE VALUE STACK, which was an object of its own.
        //
        // Stack (dispid 40) handed back an IMsgStackCom over the core's MsgStck.
        // What that object was is ONE saved (name, value) pair living INSIDE
        // this node, so it is these three members plus IsStacked above: no
        // second object and no second lifetime to get wrong.
        //
        // IT NESTS -- push, push, pop, pop unwinds -- and Pop ANSWERS whether it
        // restored anything, which the old object's Pop did not: it was a silent
        // no-op on an empty stack and still succeeded, so a drain loop written
        // against it never terminated. The bool below is the loop condition.
        [DispId(42)] void PushValue ();

        [DispId(43)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool PopValue ();

        [DispId(44)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool DropValue ();

        /// <summary>
        /// DISPID_NEWENUM. A snapshot, so a handler may delete as it walks.
        /// The CLR does not wire this to foreach by itself -- a ComImport
        /// interface is not IEnumerable -- so Msgc.Each in ComHarness.cs does
        /// the IEnumVARIANT loop once, for every collection here.
        /// </summary>
        [DispId(-4)]
        [return: MarshalAs(UnmanagedType.IUnknown)]
        object _NewEnum ();
    }

    // =======================================================================
    // IMsgAttrCom / IMsgDescCom -- the '@' attribute collection and the
    // descendant collection of one node.
    //
    // The SAME SHAPE, member for member and dispid for dispid, because the flat
    // ABI's msgcore_attr_* and msgcore_desc_* are the same shape. Two
    // interfaces rather than one because they are two collections: a node has
    // both at once, and a single type would make Attributes and Descendants
    // interchangeable in a way they are not.
    //
    // Item HERE IS ALWAYS DETACHED, unlike IMsgFieldCom.Child -- there is no
    // live accessor in the flat ABI to reach. The collection itself is live, so
    // Declare is the write path.
    // =======================================================================
    [ComImport]
    [Guid("FB553EF3-3025-4B24-9F99-894135CEB231")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgAttrCom
    {
        [DispId(0)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom Item ([MarshalAs(UnmanagedType.BStr)] string name);

        int  Count   { [DispId(1)] get; }
        bool IsEmpty { [DispId(2)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        [DispId(3)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool Exists ([MarshalAs(UnmanagedType.BStr)] string name);

        [DispId(4)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom Declare ([MarshalAs(UnmanagedType.BStr)]   string name,
                              [MarshalAs(UnmanagedType.Struct)] object value,
                              [MarshalAs(UnmanagedType.VariantBool)] bool update);

        [DispId(5)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool Delete ([MarshalAs(UnmanagedType.BStr)] string name);

        [DispId(6)] void Truncate ();

        IMsgCursorCom Cursor { [DispId(7)] [return: MarshalAs(UnmanagedType.Interface)] get; }

        [DispId(-4)]
        [return: MarshalAs(UnmanagedType.IUnknown)]
        object _NewEnum ();
    }

    [ComImport]
    [Guid("3E1B7347-6E6D-4E0D-A633-6EC6177FD70C")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgDescCom
    {
        [DispId(0)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom Item ([MarshalAs(UnmanagedType.BStr)] string name);

        int  Count   { [DispId(1)] get; }
        bool IsEmpty { [DispId(2)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        [DispId(3)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool Exists ([MarshalAs(UnmanagedType.BStr)] string name);

        [DispId(4)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom Declare ([MarshalAs(UnmanagedType.BStr)]   string name,
                              [MarshalAs(UnmanagedType.Struct)] object value,
                              [MarshalAs(UnmanagedType.VariantBool)] bool update);

        [DispId(5)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool Delete ([MarshalAs(UnmanagedType.BStr)] string name);

        [DispId(6)] void Truncate ();

        IMsgCursorCom Cursor { [DispId(7)] [return: MarshalAs(UnmanagedType.Interface)] get; }

        [DispId(-4)]
        [return: MarshalAs(UnmanagedType.IUnknown)]
        object _NewEnum ();
    }

    // =======================================================================
    // IMsgCursorCom -- a live position within a collection.
    //
    // The one object of the collection family that carries STATE, so it cannot
    // be re-resolved per call the way IMsgFieldCom is. A cursor whose mutation
    // stamp is stale rebuilds itself and seeks back to its index; _NewEnum,
    // whose contract is one pass over a fixed set, snapshots instead.
    // =======================================================================
    [ComImport]
    [Guid("14200BA9-2C01-4180-BC05-5964F6A39568")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgCursorCom
    {
        IMsgFieldCom Field { [DispId(0)] [return: MarshalAs(UnmanagedType.Interface)] get; }

        [DispId(1)] void Next ();
        [DispId(2)] void Seek ();

        [DispId(3)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool GotoName ([MarshalAs(UnmanagedType.BStr)] string name);

        [DispId(4)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool GotoIndex (int index);

        // The trap the C++ tree documents: EndOfCursor is already True ON the
        // last element, so a `while (!EndOfCursor)` loop drops it. Drive the
        // walk from Count, or from _NewEnum.
        bool   EndOfCursor   { [DispId(5)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool   StartOfCursor { [DispId(6)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        int    Count         { [DispId(7)] get; }
        int    Index         { [DispId(8)] get; }
        string Name          { [DispId(9)] [return: MarshalAs(UnmanagedType.BStr)] get; }
        bool   IsItem        { [DispId(10)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool   IsList        { [DispId(11)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool   IsVect        { [DispId(12)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        [DispId(13)] void Delete ();

        [DispId(-4)]
        [return: MarshalAs(UnmanagedType.IUnknown)]
        object _NewEnum ();
    }

    // =======================================================================
    // IMsgListCom -- a node's doubly-linked list.
    //
    // Add takes a VARIANT and picks the flat ABI's int / double / string entry
    // point from its type. A type the list cannot hold answers msgcType rather
    // than being coerced: silently storing 3.7 as 3 is worse than refusing it.
    //
    // Indexing costs a walk from the head, because a list is a linked sequence
    // and the position type it is really walked by is a raw heap address that a
    // relocation invalidates -- not something to hand to a caller. For a long
    // list use _NewEnum, which walks once.
    // =======================================================================
    [ComImport]
    [Guid("928732B8-35E6-4766-A493-64D30ECDE2AF")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgListCom
    {
        int Count { [DispId(1)] get; }

        [DispId(2)] void AddHead ([MarshalAs(UnmanagedType.Struct)] object value);
        [DispId(3)] void AddTail ([MarshalAs(UnmanagedType.Struct)] object value);
        [DispId(4)] void DropHead ();
        [DispId(5)] void DropTail ();
        [DispId(6)] void Truncate ();

        // slots 7-8: get_Item / put_Item, DISPID_VALUE. Adjacent here, but see
        // the header -- they are two methods so that this interface and
        // IMsgVectCom read identically.
        [DispId(0)]
        [return: MarshalAs(UnmanagedType.Struct)]
        object Item (int index);

        [DispId(0)]
        void Item (int index, [MarshalAs(UnmanagedType.Struct)] object value);

        [DispId(7)] int TypeAt (int index);

        [DispId(8)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool RemoveAt (int index);

        [DispId(-4)]
        [return: MarshalAs(UnmanagedType.IUnknown)]
        object _NewEnum ();
    }

    // =======================================================================
    // IMsgVectCom -- a node's indexed vector.
    //
    // THE ORDER HERE IS THE ONE TO CHECK AGAINST MIDL. get_Item is slot 1;
    // put_Item is slot 9, appended with Count long after the rest. Everything
    // between them (IsData .. get_Count) predates the setter.
    // =======================================================================
    [ComImport]
    [Guid("B96D42B3-5102-4570-A515-FF49C46B7BD7")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgVectCom
    {
        // slot 1
        [DispId(0)]
        [return: MarshalAs(UnmanagedType.Struct)]
        object Item (int index);

        // slots 2-5
        [DispId(1)] [return: MarshalAs(UnmanagedType.VariantBool)] bool IsData  (int index);
        [DispId(2)] [return: MarshalAs(UnmanagedType.VariantBool)] bool IsField (int index);
        [DispId(3)] [return: MarshalAs(UnmanagedType.VariantBool)] bool IsList  (int index);
        [DispId(4)] [return: MarshalAs(UnmanagedType.VariantBool)] bool IsVect  (int index);

        // slot 6.
        //
        // Field (dispid 5) is GONE: there is no accessor below this tier for
        // "the element at an index AS A NODE". A vect's elements are addressed
        // by index and reached as VALUES (Item), as a nested list (ListAt) or as
        // a nested vect (VectAt) -- not as nodes with paths of their own, which
        // is what an IMsgFieldCom is. NameAt answers what Field was usually
        // asked for.
        [DispId(6)] void Truncate ();

        // slot 8 -- the count the .idl promised "the day msgcore_vect_get_count
        // exists". The IID is unchanged, so a client built against the older
        // type library still binds; only a client that calls PAST the old last
        // member needs the current server.
        int Count { [DispId(7)] get; }

        // slot 9 -- the setter, here and not next to the getter.
        [DispId(0)]
        void Item (int index, [MarshalAs(UnmanagedType.Struct)] object value);

        // slots 10-16
        [DispId(8)] int TypeAt (int index);

        [DispId(9)]
        [return: MarshalAs(UnmanagedType.BStr)]
        string NameAt (int index);

        [DispId(10)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgListCom ListAt (int index);

        [DispId(11)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgVectCom VectAt (int index);

        // InsertAt (dispid 12) is gone with Field: it deep-copied a node into a
        // slot and grew the vector, and there is no insert below this tier to
        // call. A vect is declared with its length and its element prototype;
        // RemoveAt compacts, and nothing shifts the other way.
        [DispId(13)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool RemoveAt (int index);

        [DispId(-4)]
        [return: MarshalAs(UnmanagedType.IUnknown)]
        object _NewEnum ();
    }

    // =======================================================================
    // IMsgStackCom IS GONE, and so is the MsgStack coclass.
    //
    // It wrapped the core's MsgStck as an object with a lifetime of its own --
    // Push / Pop / Drop / Rename / IsEmpty / Name / Field -- and two of its
    // behaviours had to be normalised by the server rather than reported,
    // because reporting them faithfully would have been a trap: IsEmpty answered
    // False for a stack connected to nothing, which makes the obvious drain loop
    // spin, and Pop was a silent no-op when nothing was stacked, so its success
    // did NOT mean something was restored.
    //
    // What it wrapped is ONE saved (name, value) pair living inside a node, so
    // it is four members of IMsgFieldCom -- PushValue, PopValue, DropValue and
    // IsStacked -- with no second object, and Pop answering whether it restored
    // anything. Both traps go with the object.
    // =======================================================================

    // =======================================================================
    // IMsgRecursCom -- the recursive subtree walker (P2PmsgRecurs).
    //
    // A whole subtree from one flat loop. It owns a chain of cursors and
    // splices descent into its own advance, but it descends only when the
    // caller says Push -- which is what makes it a WALKER rather than an
    // iterator, and what lets a caller PRUNE by simply not descending. That is
    // the one thing For Each over Descendants cannot express.
    //
    //     var w = field.Walker;
    //     while (!w.AtEnd) {
    //         if (w.Name != "cache") w.Push();   // prune by not pushing
    //         w.MoveNext();
    //     }
    //
    // NOT a snapshot, unlike every enumerator here, because snapshotting a
    // subtree of unknown size is the thing it exists to avoid.
    // =======================================================================
    [ComImport]
    [Guid("7E31D0C6-4A92-4B77-8E05-9C6B3F214AD8")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgRecursCom
    {
        [DispId(1)] void MoveNext ();
        [DispId(2)] int  Push ();
        [DispId(3)] int  Pop ();
        [DispId(4)] void Break ();

        bool   AtEnd { [DispId(5)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        string Name  { [DispId(0)] [return: MarshalAs(UnmanagedType.BStr)] get; }

        bool IsField { [DispId(6)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool IsList  { [DispId(7)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }
        bool IsVect  { [DispId(8)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        IMsgFieldCom Field  { [DispId(9)]  [return: MarshalAs(UnmanagedType.Interface)] get; }
        IMsgListCom  List   { [DispId(10)] [return: MarshalAs(UnmanagedType.Interface)] get; }
        IMsgVectCom  Vector { [DispId(11)] [return: MarshalAs(UnmanagedType.Interface)] get; }

        /// <summary>
        /// New, and both come from the same place: the walker reports where it
        /// is, and the server assembles a path from that as the walk moves.
        /// Depth is 0 at the outermost level; Path is the current stop's, in the
        /// spelling MsgStore.FieldAt takes -- so unlike everything else on this
        /// object it stays true after the walk has moved on.
        /// </summary>
        int    Depth { [DispId(12)] get; }
        string Path  { [DispId(13)] [return: MarshalAs(UnmanagedType.BStr)] get; }
    }

    // =======================================================================
    // IMsgStoreCom -- the one creatable object. A store IS a document.
    //
    // Each CoCreateInstance is its own store, unlike TargetCom's P2PNetwork,
    // which wraps a process-wide singleton. It owns every object it hands out
    // transitively, so a store cannot be destroyed while anything still points
    // into its heap: Close() is about WHEN, not whether.
    // =======================================================================
    [ComImport]
    [Guid("A15B2AEA-8560-43CF-AC8B-144DBCBDAB14")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgStoreCom
    {
        [DispId(1)] void Open ([MarshalAs(UnmanagedType.BStr)] string path);

        /// <summary>An empty path saves back over Filename; msgcSave if there is none.</summary>
        [DispId(2)] void Save ([MarshalAs(UnmanagedType.BStr)] string path);

        [DispId(3)] void Close ();

        /// <summary>
        /// Was Nullify. Empty the store, keeping it open AND KEEPING ITS
        /// FILENAME -- it is the same store rather than a rebuilt one, so a
        /// later Save() with no path writes the emptied store back over the file
        /// it came from. Every node reference a client is holding stays valid
        /// and reports msgcStale.
        ///
        /// (The rebuild still happens, one layer down, once for every client:
        /// the core's own Nullify closes the heap and leaves the manager
        /// answering is_valid while pointing at nothing.)
        /// </summary>
        [DispId(4)] void Clear ();

        /// <summary>
        /// RENAMES THE ROOT NODE -- the inverse of RootName, and something this
        /// tier could not do at all before. The old member here was RenameFile,
        /// a MoveFileEx on the store's FILE, which is a host's job and not this
        /// server's. msgcName for an unusable name.
        /// </summary>
        [DispId(5)] void RenameRoot ([MarshalAs(UnmanagedType.BStr)] string newName);

        string Filename { [DispId(6)] [return: MarshalAs(UnmanagedType.BStr)] get; }
        string RootName { [DispId(7)] [return: MarshalAs(UnmanagedType.BStr)] get; }

        bool Dirty
        {
            [DispId(8)] [return: MarshalAs(UnmanagedType.VariantBool)] get;
            [DispId(8)] [param:  MarshalAs(UnmanagedType.VariantBool)] set;
        }

        int  Size    { [DispId(9)]  get; }
        bool IsValid { [DispId(10)] [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        IMsgFieldCom Root { [DispId(11)] [return: MarshalAs(UnmanagedType.Interface)] get; }

        /// <summary>
        /// A STRING is a path (".Config.Window.Width", "" for the root); a
        /// NUMBER is a P2Pos. BOTH ANSWER THE SAME KIND OF OBJECT NOW: a
        /// writable node. The asymmetry this method used to carry -- a path gave
        /// a live node and a P2Pos gave a detached copy whose writes vanished --
        /// was the deep copy one layer down, and it is gone.
        ///
        /// A P2Pos whose node has been DELETED is msgcNoPos rather than the
        /// debug break it used to be: the search that resolves it walks the LIVE
        /// tree, so a freed block is simply not found. The msgcTriggerDelete
        /// event is the one case that hands you such a P2Pos, and it is now safe
        /// to try -- though it is still an IDENTITY to compare against first.
        ///
        /// A path that does not PARSE is msgcPath; one that parses and names
        /// nothing is msgcNoField. Three questions, three answers.
        /// </summary>
        [DispId(12)]
        [return: MarshalAs(UnmanagedType.Interface)]
        IMsgFieldCom FieldAt ([MarshalAs(UnmanagedType.Struct)] object pathOrPos);

        /// <summary>
        /// THE SAME SPELLING FieldAt TAKES, which it was not: this used to
        /// answer the kernel's own grammar, with the root's NAME as the first
        /// segment, so feeding it back found nothing and the doc comment had to
        /// say so. A path handed out can now be handed back.
        /// </summary>
        [DispId(13)]
        [return: MarshalAs(UnmanagedType.BStr)]
        string PathOf (long p2pos);

        [DispId(14)] void CreateNew (int addrMode, int initialBytes, int maxBytes);

        // --- change notification ---------------------------------------------
        // DELETE fires by itself when a node is freed; INSERT and UPDATE fire
        // when a writer calls FireTrigger. Msgcore does not detect its own
        // mutations, it reports the ones it is told about.
        [DispId(15)] void ArmTrigger    (int mask, long p2pos);
        [DispId(16)] void DisarmTrigger (int mask, long p2pos);
        [DispId(17)] int  FireTrigger   (int mask, long p2pos);

        // --- utilities --------------------------------------------------------
        // On the store rather than free-standing because a scripting host has
        // nowhere to put a free function.
        [DispId(18)]
        [return: MarshalAs(UnmanagedType.BStr)]
        string TypeName (int dataType);

        [DispId(19)] int TypeFromName ([MarshalAs(UnmanagedType.BStr)] string typeName);

        [DispId(20)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool WildcardMatch ([MarshalAs(UnmanagedType.BStr)] string pattern,
                            [MarshalAs(UnmanagedType.BStr)] string name);

        /// <summary>
        /// This server's build tag AND the layers below it -- one string that
        /// answers "what am I talking to" all the way down. It used to be this
        /// server's alone.
        /// </summary>
        string VersionString { [DispId(21)] [return: MarshalAs(UnmanagedType.BStr)] get; }

        /// <summary>
        /// Ask whether a name is usable BEFORE declaring it, rather than
        /// declaring and reading the error. New at this tier, and worth having
        /// wherever names come from user input: 1 to 63 UTF-16 units and none of
        /// . @ : ^ / \ * ? | &lt; &gt; or " -- the first two because they are the
        /// path grammar's separators.
        ///
        /// DECLARED HERE, before SetPagingSink, because that is where the .idl
        /// declares it and DECLARATION ORDER IS VTABLE ORDER. Its dispid (28) is
        /// higher than the paging members' and says nothing about its slot.
        /// </summary>
        [DispId(28)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool IsValidName ([MarshalAs(UnmanagedType.BStr)] string name);

        // --- paging -----------------------------------------------------------
        //
        // READ THE SINK'S CONTRACT BEFORE WIRING ONE. Paging is the inverse of
        // OnChange at every point: raised on the ACCESSING thread, during the
        // access, with the store lock HELD and the core blocked waiting; the
        // return value IS the answer; a handler must not block and must not
        // re-enter beyond the subtree it was asked for.
        //
        // Which is why it is a directly-registered sink and not a connection
        // point: marshalling a call the core is synchronously waiting on, under
        // a lock it already holds, is a deadlock. SetPagingSink refuses a sink
        // registered from another apartment for exactly that reason.
        [DispId(22)]
        void SetPagingSink ([MarshalAs(UnmanagedType.Interface)] IMsgPagingSink sink);

        [DispId(23)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool PageIn ([MarshalAs(UnmanagedType.Struct)] object p2pos);

        [DispId(24)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool PageOut ([MarshalAs(UnmanagedType.Struct)] object p2pos,
                      [MarshalAs(UnmanagedType.VariantBool)] bool flush);

        // PageSumm (dispid 25) is gone: it recounted a paged node after
        // additions and removals, and is not exposed below this tier.

        /// <summary>
        /// Suspend the registration for a section of work -- what the C++
        /// SafeRegistrationPush does with a constructor and a destructor. A Save
        /// is the usual reason. These DO NOT NEST: a second push answers
        /// msgcPageState rather than losing the first saved set.
        /// </summary>
        [DispId(26)] void PushPaging ();
        [DispId(27)] void PopPaging ();
    }

    // =======================================================================
    // IMsgPagingSink -- implemented by the HOST, not by the server.
    //
    // Return True for "done, the data is resident". Anything else is taken as a
    // refusal and reported to the caller that provoked it; an error is not
    // propagated as an exception, because the core cannot unwind through a page
    // fault. A managed class implements this the ordinary way -- see
    // Msgc.PagingSink in ComHarness.cs -- which is the whole demonstration:
    // the C++ tree needs a hand-written IDispatch for the same three methods.
    // =======================================================================
    [ComImport]
    [Guid("5F0A3B29-7C41-4D96-8A2E-B3D7016E4F58")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    public interface IMsgPagingSink
    {
        [DispId(1)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool OnPageIn ([MarshalAs(UnmanagedType.Struct)] object p2pos);

        [DispId(2)]
        [return: MarshalAs(UnmanagedType.VariantBool)]
        bool OnPageOut ([MarshalAs(UnmanagedType.Struct)] object p2pos,
                        [MarshalAs(UnmanagedType.VariantBool)] bool flush);

        // OnPopulate (dispid 3) is gone: it was the core's third paging
        // callback, it is not exposed below this tier, and nothing raises it.
        // A sink that still declared it would have the wrong vtable.
    }

    // =======================================================================
    // _IMsgStoreEvents -- the source dispinterface, raised on a dedicated
    // dispatch thread and marshalled into the sink's apartment.
    //
    // `path` travels with the queued copy because by the time this is raised
    // the node may be gone. IT IS ALWAYS EMPTY FOR msgcTriggerDelete: that
    // trigger fires from inside P2PmsgHeap_FreeBSTRio AFTER the block has been
    // freed, so at the only moment the path could be captured the node is
    // already gone. What identifies WHICH node went is the P2Pos -- and see the
    // warning on FieldAt about resolving it.
    // =======================================================================
    [ComImport]
    [Guid("AE287CFB-B7C6-451F-B6DA-EB583E32A7AE")]
    [InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
    public interface IMsgStoreEvents
    {
        [DispId(1)] void OnChange (int kind, long p2pos,
                                   [MarshalAs(UnmanagedType.BStr)] string path);
        [DispId(2)] void OnError  ([MarshalAs(UnmanagedType.BStr)] string what);
    }

    /// <summary>coclass MsgcoreCom.MsgStore -- the only creatable one.</summary>
    [ComImport]
    [Guid("C421007F-17C2-4904-8F6D-7EA3B8108827")]
    public class MsgStoreClass { }

    // =======================================================================
    // The enums, transcribed from the library block.
    // =======================================================================

    /// <summary>The VBLockData_* vocabulary. What DeclareTyped takes and what TypeName spells.</summary>
    public static class MsgDataType
    {
        public const int Null   = 0;
        public const int Int08  = 1;
        public const int UInt08 = 2;
        public const int Int16  = 3;
        public const int UInt16 = 4;
        public const int Int32  = 5;
        public const int UInt32 = 6;
        public const int Int64  = 7;
        public const int UInt64 = 8;
        public const int Float  = 9;
        public const int Double = 10;
        public const int Bool   = 13;
        public const int Bstr   = 18;
        public const int WStr   = 26;
        public const int Blob   = 34;
        public const int Guid   = 47;
    }

    /// <summary>Fixed at CreateNew and never afterwards: it decides the on-disk layout.</summary>
    public static class MsgAddrMode
    {
        public const int Addr16 = 1;
        public const int Addr32 = 2;
        public const int Addr64 = 3;
    }

    /// <summary>What ArmTrigger takes. One OnChange carries exactly one bit.</summary>
    [Flags]
    public enum MsgTriggerFlag
    {
        Insert = 1,
        Update = 2,
        Delete = 4,
        Active = 8,
        All    = 15
    }

    /// <summary>
    /// The HRESULTs MsgcoreCom defines, plus the standard ones these harnesses
    /// branch on. The ones worth branching on in ordinary code are msgcForeign
    /// (an object from another store), msgcStale (the node this object named is
    /// gone) and msgcPath (that string is not a path).
    /// </summary>
    public static class Hr
    {
        public const int S_OK    = 0;
        public const int S_FALSE = 1;

        public const int E_POINTER           = unchecked((int)0x80004003);
        public const int E_INVALIDARG        = unchecked((int)0x80070057);
        public const int DISP_E_TYPEMISMATCH = unchecked((int)0x80020005);
        public const int DISP_E_MEMBERNOTFOUND = unchecked((int)0x80020003);
        public const int DISP_E_UNKNOWNNAME  = unchecked((int)0x80020006);
        public const int REGDB_E_CLASSNOTREG = unchecked((int)0x80040154);

        public const int msgcClosed    = unchecked((int)0x80040300);
        public const int msgcLoad      = unchecked((int)0x80040301);
        public const int msgcSave      = unchecked((int)0x80040302);
        public const int msgcNoField   = unchecked((int)0x80040303);
        public const int msgcNoPos     = unchecked((int)0x80040304);
        public const int msgcType      = unchecked((int)0x80040305);
        public const int msgcDeclare   = unchecked((int)0x80040306);
        public const int msgcNotList   = unchecked((int)0x80040307);
        public const int msgcNotVect   = unchecked((int)0x80040308);
        public const int msgcNoColl    = unchecked((int)0x80040309);
        public const int msgcRange     = unchecked((int)0x8004030A);

        /// <summary>
        /// RESERVED AND UNREACHABLE. 0x8004030B was msgcDetached -- "you wrote
        /// through a copy and the write went nowhere". There is no detached node
        /// any more, so nothing can answer it. Kept, spelled so that nothing
        /// tests for it by accident, because the number is documented and a hole
        /// costs more to explain than the constant does to keep.
        /// </summary>
        public const int msgcDetached_Reserved = unchecked((int)0x8004030B);

        public const int msgcKernel    = unchecked((int)0x8004030C);
        public const int msgcStale     = unchecked((int)0x8004030D);
        public const int msgcName      = unchecked((int)0x8004030E);
        public const int msgcNoSink    = unchecked((int)0x8004030F);
        public const int msgcForeign   = unchecked((int)0x80040310);
        public const int msgcPageState = unchecked((int)0x80040311);
        public const int msgcPath      = unchecked((int)0x80040312);
        public const int msgcLimit     = unchecked((int)0x80040313);

        /// <summary>The FAILED() macro. COM severity lives in the top bit.</summary>
        public static bool Failed (int hr) { return hr < 0; }

        public static string Name (int hr)
        {
            switch (hr)
            {
                case S_OK:                   return "S_OK";
                case S_FALSE:                return "S_FALSE";
                case E_POINTER:              return "E_POINTER";
                case E_INVALIDARG:           return "E_INVALIDARG";
                case DISP_E_TYPEMISMATCH:    return "DISP_E_TYPEMISMATCH";
                case DISP_E_MEMBERNOTFOUND:  return "DISP_E_MEMBERNOTFOUND";
                case DISP_E_UNKNOWNNAME:     return "DISP_E_UNKNOWNNAME";
                case REGDB_E_CLASSNOTREG:    return "REGDB_E_CLASSNOTREG";
                case msgcClosed:             return "msgcClosed";
                case msgcLoad:               return "msgcLoad";
                case msgcSave:               return "msgcSave";
                case msgcNoField:            return "msgcNoField";
                case msgcNoPos:              return "msgcNoPos";
                case msgcType:               return "msgcType";
                case msgcDeclare:            return "msgcDeclare";
                case msgcNotList:            return "msgcNotList";
                case msgcNotVect:            return "msgcNotVect";
                case msgcNoColl:             return "msgcNoColl";
                case msgcRange:              return "msgcRange";
                case msgcDetached_Reserved:  return "msgcDetached (reserved, unreachable)";
                case msgcKernel:             return "msgcKernel";
                case msgcStale:              return "msgcStale";
                case msgcName:               return "msgcName";
                case msgcNoSink:             return "msgcNoSink";
                case msgcForeign:            return "msgcForeign";
                case msgcPageState:          return "msgcPageState";
                case msgcPath:               return "msgcPath";
                case msgcLimit:              return "msgcLimit";
                default:                     return "(other)";
            }
        }
    }
}
