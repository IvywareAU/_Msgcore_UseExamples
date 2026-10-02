# FieldAccessTest — field access by name, `MsgFieldRef.hpp`

Follows [`ListVectTest`](../ListVectTest). Still no networking.

The plain API already reaches a field by name; what it costs is ceremony —
a declare to write, a select and a typed accessor to read. `Msgcore/MsgFieldRef.hpp`
is a header-only layer that turns both into assignment, in the two forms C++
allows:

| | Dynamic | Typed view |
| --- | --- | --- |
| Write | `Field(item, L"uptime") = 86400;` | `msg->uptime = 86400;` |
| Read | `Field(item, L"uptime").AsInt()` | `int up = msg->uptime;` |
| Names known | at run time | at compile time (`MSG_FIELD`) |
| Wrong type | throws `P2Pevent*` on read | does not compile |

`msg->unknownName` is the one thing neither form can do: C++ has no hook that
turns an undeclared member into a lookup.

## What it covers

1. **The dynamic form** — every type (`int`, `long long`, `double`, `bool`, wide
   text, UTF-8 `const char*`, `MsgBlob`), each read back through the proxy and
   through the plain API, and a rewrite that changes a field's type.
2. **Nesting** — `Field(item, L"pos")[L"x"]`: a write creates the path, a read
   creates nothing, and refs keep finding their own field while siblings are
   written around them.
3. **A typed view** — `MSG_FIELD` members, conversion on read, field-to-field
   copy, `Exists`/`Erase`, and the dynamic form on the same view.
4. **Two codings** — `Typed` (each type under its own tag) and `Bytes` (every
   value a blob, TargetFacade's field format), with readers that accept either.
5. **What is refused** — an absent field, the wrong type, a 64-unit name, and a
   view that was never bound.

## Two things worth knowing before you use it

- **A ref holds names, not items.** `SelectItem` hands back the parent's *cursor*
  item, and the next lookup on that parent silently retargets it. So every read
  and write re-resolves from the anchor — a cursor walk per access, which is the
  price of `msg->x`.
- **An overlong name is refused before the tree is touched**, with a message
  that names the field. Writing this harness is what found that a plain
  `DeclareItem` with a name past 63 UTF-16 units corrupted the heap rather than
  throwing cleanly. That was fixed in Msgcore on 2026-10-02.

Design record: `MsgFieldAccessPlan.md` at the MSCS root.
