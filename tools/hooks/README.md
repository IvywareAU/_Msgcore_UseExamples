# tools/hooks

Git hooks for this repository, tracked so they can be reviewed and so a fresh clone can
install them. Git does not run anything from here until you point it at this directory —
`.git/hooks/` is what Git executes, and it is not version-controlled.

## Install

```sh
git config core.hooksPath tools/hooks
```

Per clone, and per developer. There is no way to make a hook install itself; a repository
that could run code on clone would be a supply-chain hole, not a feature.

To uninstall: `git config --unset core.hooksPath`.

## `pre-push`

Keeps three things off `master`: deletion, non-fast-forward pushes, and any commit whose
exact SHA has not already gone green in the `CI` workflow. Other branches push freely,
which is deliberate — pushing a branch is how CI gets to run at all.

The third rule implies the workflow a server-side required-status-check would have forced:

```sh
git switch -c fix-something
git push -u origin fix-something      # CI runs on this SHA
# ... wait for green ...
git switch master
git merge --ff-only fix-something     # SHA preserved, so it stays green
git push
```

`--ff-only` is load-bearing. A merge commit is a new SHA that CI has never seen, and the
hook refuses it exactly as it refuses any other unverified commit.

## Know what the green tick it waits for actually means

The `CI` workflow this hook consults **compiles nothing** — it cannot, because this
repository does not build on its own (see the README's "The sibling dependencies"). It
verifies bookkeeping: solution/project agreement, sources present, sibling bindings
unchanged, Markdown links resolving.

So rule 3 keeps *unchecked* commits off `master`, not *unbuilt* ones. The build-and-run
verification lives in `solution-build.yml`, which is `workflow_dispatch`-only and is not
what this hook waits for. Do not read a passing push as "it still builds".

## What this is not

**It is not branch protection.** It is a script on one machine, and it fails open in every
direction that matters:

- it runs only for someone who ran the install command above;
- `git push --no-verify` skips it entirely — by design, as the escape hatch standing in for
  the admin bypass GitHub would have provided;
- anyone who can push can edit or delete it.

It exists because server-side enforcement is unavailable: GitHub Free does not offer
branch protection or rulesets on **private** repositories, and both APIs answer
`403 Upgrade to GitHub Pro or make this repository public`.

When server-side rules do become available, **replace this hook rather than keeping both**.
Two enforcement points that can disagree are worse than one that cannot be bypassed.
