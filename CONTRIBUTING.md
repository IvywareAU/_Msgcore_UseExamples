# Contributing to `_Msgcore_UseExamples`

## Before you spend time on a change

This repository is **not self-contained** — a fresh clone does not compile, because
`Msgcore/`, `MsgFacade/`, `Targetcore/`, `TargetFacade/` and `vsutils/` are peer
directories in a parent solution that is not published here. The README's ["The sibling
dependencies"](README.md#the-sibling-dependencies) lists every binding. Until that is
resolved an outside contributor cannot build what they are changing, which makes anything
beyond a documentation fix hard to do well.

Issues and reports are welcome regardless.

## What this repository is for

These are **worked examples** of the Msgcore data model, not a test suite and not a
library. It holds four trees — [`DirectExamples`](DirectExamples),
[`FacadeExamples`](FacadeExamples), [`ComExamples`](ComExamples) and
[`dotNetExamples`](dotNetExamples) — which are the same eight subjects reached through
four different bindings; the root [`README.md`](README.md) says which is which. That
changes what a good contribution looks like:

- A harness earns its place by answering **one question** that no other harness here
  answers, and by saying at the top of the file what that question is. The eight are
  ordered so each builds on the one before; a ninth should extend that line rather than
  restate it.
- The header comment is the deliverable as much as the code is. The existing files are
  the standard: what is being asked, what the Msgcore source actually says about it
  (cited by file and line), what was verified rather than assumed, and what is still
  unknown.
- **Do not quietly fix the library from in here.** If a harness reveals an Msgcore
  defect, the harness records it — several of these do, in a "what this found" section —
  and the fix goes in `Msgcore`. An example that works around a bug teaches the
  workaround, not the API.

## Sign your work — the Developer Certificate of Origin

Every commit must carry a `Signed-off-by` line:

```
Signed-off-by: Jane Developer <jane@example.com>
```

`git commit -s` adds it for you. Use your real name and an address you read.

That line means you certify the [Developer Certificate of Origin
1.1](https://developercertificate.org/): that you wrote the contribution or otherwise
have the right to submit it under the Apache License, Version 2.0, and that you
understand the contribution and its record are public and permanent.

**Why this is enforced from the first commit rather than added later.** Only a rights
holder can license code. Once a contribution arrives with no record of who held the
rights and under what terms, the project can no longer answer that question for its own
tree — and the option of ever relicensing, dual-licensing or granting an exception closes
permanently, because there is nobody identifiable to ask. A pull request without a
sign-off cannot be merged, no matter how good it is.

## Making a change

1. **One concern per commit.** A refactor and a behaviour change in the same commit
   cannot be reviewed, reverted or bisected independently.
2. **Write the message for someone reading it in five years.** The subject line as an
   imperative sentence, the body for the reasoning. The existing history is the standard
   to match: "Close the coverage gap: the recursive walker and the heap", "Keep one
   solution, and put the tree under Apache-2.0".
3. **Build both configurations**, `Debug` and `Release` × `x64`.
4. **Run `run_all.ps1` in both configurations** and put the resulting table in the pull
   request. A harness that has not been run is a hypothesis.
5. **Keep the exit-code contract.** `0` success, `1` setup, `2` assertion, `3` a failed
   check — see the README. A harness that prints "FAILED" and returns `0` cannot be
   adjudicated by anything.

## Adding a harness

A ninth subject belongs in **all four trees or in none**. The trees are worth reading
side by side only for as long as they cover the same list; one tree drifting ahead is
what makes the comparison stop working.

Alongside the code, in each tree that has a solution:

- register it in that tree's `.sln` — `DirectExamples(2026).sln`,
  `FacadeExamples(2026).sln`, `ComExamples(2026).sln` — for **both** configurations. The
  `ci.yml` invariants job fails on a project no solution builds;
- in `DirectExamples`, give it a `README.md` in its own directory. All eight have one,
  and the invariants job checks for it. The other three trees document themselves in one
  README each, deliberately;
- add it to that tree's `run_all.ps1`, and to `dotNetExamples\build.ps1`;
- point `OutDir`/`IntDir` at the tree's shared `out\` root like every other project, and
  do not add a per-project `.sln`.

## Moving or renaming a tree

Every outward path in this repository is relative, so a tree that changes depth breaks
paths in four kinds of file at once: `.vcxproj`, `common\*.props`, `run_all.ps1` /
`build.ps1`, and the prose. Two of those fail loudly and two do not.

- The pins in `.github/ci/check_repo_invariants.py` are what catch it. They record the
  outward paths **and their depth** per tree; a level lost in a move shows up there
  rather than as `LNK1181` later.
- `git mv` the `.sln` too. A `.sln` never contains its own name, so no grep will tell you
  it was left behind — only a filename listing will, and `run_all.ps1` naming a file that
  no longer exists fails as "project file does not exist".

## Two things that will get a change rejected on sight

- **A new sibling dependency added silently.** The bindings listed in the README are
  pinned by the invariants job precisely so that the standalone-build story cannot get
  worse without a reviewer noticing. If a change needs another, change the README and the
  pin in the same commit and argue for it.
- **Deleting a comment that records a hazard** because the code around it was fixed.
  Rewrite it to say what is true now. Several of these headers exist only because
  somebody wrote down what bit them.

## License

By contributing, you agree that your contributions are licensed under the Apache License,
Version 2.0. See [`LICENSE`](LICENSE).
