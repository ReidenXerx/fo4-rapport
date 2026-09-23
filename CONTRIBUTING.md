# Contributing to Rapport

The short version: **a claim from reading rather than running is unverified**, and this project has
repeatedly had a confident census of AAF's own XML contradicted by AAF itself. When the game and a
static count disagree, the game wins and the count was measuring the wrong thing.

Everything below is a rule that cost something to learn.

## Before you write any code

1. Read **`DESIGN.md`**. It is the specification and it outranks every other file including this
   one. The amendments at the bottom (`A-1` onward) are decisions with their reasons; where a
   section above disagrees with an amendment, the amendment wins.
2. Read **`docs/aaf-under-the-hood.md`** before integrating anything new with AAF. It is 21 numbered
   findings about how AAF actually behaves rather than what its API looks like it promises, each
   with how it was established so it is re-checkable when AAF changes.
3. Read **`docs/FEATURES.md`** to see what is verified in game and what is merely built. If you are
   about to rely on something in the second list, verify it first.

## The four rules that cost the most to learn

**1. The plugin may NEVER call into the Papyrus VM.** It crashed the game twice inside
`DispatchMethodCallImpl`: F4SE tasks run on a `BSJobs` job thread, and the VM packs call arguments
through the per-thread scrap heap. The architecture is a **doorbell** — Papyrus polls native
functions, C++ answers. C++ queues `Order`s; the bridge drains them, one per stack via
`CallFunctionNoWait`. Anything that reverses that direction is wrong no matter how well it seems to
work in a test.

**2. `Bridge.psc` has exactly ONE timer** (`kPollTimer = 1`), and never calls AAF on the timer's own
stack. The measured failure was the AAF call: a stack that reaches `StartScene` often never comes back,
and `OnTimer` runs one at a time per script, so one stuck poll starves every timer on it. The second id
first blamed for it was never at fault (`docs/two-lifetimes.md`, corrected 2026-09-23). The next poll is
scheduled BEFORE any AAF call, and every AAF call goes out through `CallFunctionNoWait`.

**3. A failed `as` cast in Papyrus assigns None.** Land that on a loop counter and the loop never
increments — one such bug wrote 845,998 lines and 912 MB. Check every `as` inside a loop.

**4. Arrays live in the SAVE.** A struct that changes shape makes the saved array come back as
`None`, not empty. Never assume an array is non-None; a load is a new lifetime for Papyrus but NOT
for the plugin.

## What counts as evidence here

| Claim | What settles it |
| --- | --- |
| "AAF does X" | A running game, or AAF's own decompiled source. Not a wiki, not memory, not a count of XML files. |
| "This feature works" | A log line from a real session, or somebody watching it on screen. Both, for anything visible. |
| "Nothing uses this" | A search that is stated, with its scope. A census can say "nobody does this" and can never say "it cannot be done" — the slot-0 blush took one launch to settle after the census had already predicted the answer. |
| "The tests pass" | Only if the test can fail. A round-trip check that scans a generated binary for plausible strings passed on a file that was two bytes wrong in its header. |

**Any subsystem whose healthy state and broken state produce the same log needs a counter, not an
argument.** A poll with nothing to do and a poll that never happened write identical lines; two
whole debugging runs were spent on the wrong subsystem before the bridge got a poll counter.

## Things that are settled — do not reopen without new evidence

- **Do not reinstate `ChangePosition`.** Refused 26 times out of 26 — with tags, with a named
  position id, and with no filters at all — for content that demonstrably exists.
  `FindMatchingAnimations` returned 0 for all 17 tags queried while the unfiltered query came back
  non-zero. Staging is AAF's; Rapport picks the tree the scene starts on.
- **Do not modify AAF's XML to work around it.** Rejected: it fights another mod's deliberate design
  and changes behaviour for every AAF mod on the install.
- **Do not rebuild `FindMatchingAnimations` as a pre-check.** It is asynchronous, its argument
  layout has never been observed in this project, and a tag index knows a tag exists *somewhere*,
  never whether an animation exists for this pair, in this furniture, here. AAF's own refusal is the
  only real evidence.
- **Do not pass `startEquipmentSet`.** It replaces AAF's automatic undressing rather than adding to
  it: both actors stayed fully dressed through a complete sex scene, with nothing in any log saying
  so. `ApplyEquipmentSet` is a genuinely open question — it has never been tested in isolation.
- **Anything that names another mod's script types lives in its own optional plugin.** A script that
  names a type nobody has installed carries a reference the VM cannot resolve. Putting that in
  `Rapport:Bridge` would risk every install to gain one integration. `GlobalVariable`s are different
  — they are a vanilla type and need no optional plugin.

## Design rules for new features

**If a second mod would also want it, it belongs in Rapport rather than in an addon.** That is the
whole reason the framework exists. Conversely, a judgement about *when* something should happen —
"too soon", "bored of this partner", "wants company" — is policy and belongs in an addon.

**Nothing Rapport applies may ever be something only Rapport can remove.** Every piece of state this
mod puts on an NPC outlives what put it there. So each one is written into the save, cleaned on
load, and removable in one switch (`PanicClear`) that is run *before* uninstalling.

**Automatic, but never secret.** Where Rapport configures or stops another mod, it detects before
acting, records the previous state, can be switched off individually, and names the change in the
log with its reason. A mod that silently changes another mod's settings is indistinguishable, from
the outside, from a mod that breaks it.

**Say it out loud when it is nothing.** `ledger: after loading, 0 actor(s)` is a deliberate line:
without it, "the ledger loaded and was empty" looks identical to "the ledger never loaded".

## Building and testing

```
cmake --build build --config Release
scripts/build-papyrus.ps1
scripts/deploy-dev.ps1          # refuses while Fallout4.exe is running
```

`scripts/*.ps1` take their paths as parameters. **Do not commit your own paths as the defaults** —
see the note in the issue template about what not to paste into a bug report, which applies here
too.

**Papyrus logging stays ON during development.** Turning it off to be tidy cost a night: five
debugging runs happened with a Papyrus stack dying on every one of them while the VM's own account
of exactly that was switched off. `debug` is the development profile in `debug.json`; `off` is the
shipping default, and Rapport warns loudly at startup whenever a non-`off` profile is active.

Never compile into `Data/Scripts` — that directory is deployed by your mod manager.

**Run `tools/tagaudit.py` after installing any animation pack.** A tag with no rule is
indistinguishable in the log from a scene that was only kissing, and the script is the only thing
that separates them.

## When you change something

- If it changes behaviour AAF is involved in, add the measurement to `docs/aaf-under-the-hood.md`
  with how it was established — including a negative result. A thing that was tried and did not work
  is worth more than a thing that was never tried, because it stops the next person spending a night
  on it.
- If it changes a decision, add an amendment to `DESIGN.md` rather than editing the section above.
- If it changes the verified/unverified status of a capability, update `docs/FEATURES.md`. That
  distinction is the most useful thing in the repository and it rots fastest.
- Put the raw log in `docs/runs/` if it is the evidence for a claim.

## Commit messages

Say what changed and why, in the voice of the thing that broke. The existing history is the style
guide: *"A Papyrus stack does not come back from an AAF call"*, *"The poll was never dying:
FindRequestByActors was spinning forever"*, *"startEquipmentSet REPLACES AAF's undressing, so do it
at runtime instead"*.
