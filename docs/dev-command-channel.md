# The dev command channel

A way to drive, observe and photograph a **running** Fallout 4 from outside it, so testing a change
does not cost a build, a deploy, a relaunch and a walk across Diamond City.

Built 2026-09-20. Everything marked ✅ was run in a live game; everything marked ⬜ was not.

---

## Why it exists

Before this, verifying one fix looked like: build, deploy, relaunch, load, walk to an NPC, ask the
owner whether she looked shiny. The stranded-overlay requeue took **three hours** and still ended
half-proved, because proving it meant finding one named NPC on foot.

With the channel the same test is four commands and about ten seconds.

---

## Shape

Three independent channels. Their independence is the point: when the log says an overlay was
removed and the screenshot shows sweat, that disagreement is a bug found without anyone noticing it
by eye.

| | what | where |
| --- | --- | --- |
| **Drive** | verbs in, answers out | `Rapport.cmd` / `Rapport.cmd.out` beside `Rapport.log` |
| **See** | a photograph of the game window | `scratchpad/shot.ps1`, `shot-stable.ps1` |
| **Know** | what the plugin believes | `Rapport.log`, and `Rapport.prev.log` from the run before |

```
<F4SE log dir>/Rapport.cmd      one verb per line; RENAMED then read, then deleted
<F4SE log dir>/Rapport.cmd.out  the line, then its answer, appended
```

Round trip is about 1.5s: a 250ms watcher poll plus the reader's tail.

No sockets, no ports, nothing to install. An MCP server on top is a small process that writes one
file and tails another.

## Architecture, and the two rules that make it safe

**IO on a watcher thread, work on the main thread.** The watcher does file IO only; anything that
touches a form or plugin state goes through `F4SE::GetTaskInterface()->AddTask`. This is the same
rule the codebase keeps about never reaching the Papyrus VM from a worker, for the same reason.

**The inbox is renamed, then read.** The first version read the file and then deleted it, and
anything appended between those two steps was deleted unread — it silently ate the first teleport
command sent at it, and the failure looked exactly like the command never having been sent. A rename
is atomic within a volume: a writer appending a moment later creates a fresh file and loses nothing,
while keeping the property the delete was for — what is about to run is already out of the inbox, so
a command that takes the game down is not re-run on the next launch.

## Turning it on

Two flags in `Rapport.ini`, both `0` in the build that ships:

```ini
DevMailbox=0    ; opens the channel
DevConsole=0    ; needed ON TOP for the console verb
```

They are two flags rather than one level deliberately: opening something that reports plugin state
and opening something that runs arbitrary commands are different decisions, and nobody should make
the second by accident while making the first.

`scripts/deploy-dev.ps1` flips `DevMailbox=1` in the **staging** copy after copying. That is the only
place the difference between "the dev build" and "the build" is real rather than a promise. Config is
read at plugin **load**, so a change needs a restart. When the channel is open the plugin says so at
**error** level on every launch, deliberately — a dev flag's only real defence is being impossible to
leave on by accident.

---

## The verbs

Form ids are **hex by default**; `d:<n>` for decimal.

### Observing — read-only, no side effects

| verb | does | |
| --- | --- | --- |
| `ping` | proves the channel | ✅ |
| `nearby [radius]` | every loaded actor: name, form id, distance, `[AAF busy]`, `[no 3D]` | ✅ |
| `who <id>` | one actor: loaded, AAF-busy | ✅ |
| `state <id>` | the full engine picture — see below | ✅ |
| `health` | bridge state, stranded count, heal count | ✅ |
| `stranded` | cleanup orders held for absent actors | ✅ |
| `gametime` | hour since the save began, day, clock time | ✅ |

`nearby` ended the practice of grepping the log for `would pair` lines to learn form ids — that only
ever named the top three of a ranked list, and only when the scheduler happened to tick.

`state <id>` answers in the **log**, one poll later, not in the mailbox reply: almost none of these
queries exist on the C++ side, so it rides the doorbell.

```
state: 00002F0B (12043) loaded=True scene=False combat=False talking=False weapon=False
       sneaking=False dead=False unconscious=False sit=0 sleep=0 relationship=0 dist=333 heading=108deg
```

`relationship` is `GetRelationshipRank` against the player — an engine-held value Chemistry has never
consulted. `heading` is **relative to the player's current facing**, not a world yaw.

### Acting on the world

| verb | does | |
| --- | --- | --- |
| `request <a> <b> [scenario]` | start a Rapport scene on demand | ✅ |
| `pause` / `resume` | hold autonomy off the scene slot | ✅ |
| `heal` | give up on the scene in flight | ⬜ |
| `bring <id>` | teleport an actor to the player | ✅ |
| `goto <id>` | teleport the player to an actor | ✅ |
| `travel <id>` | **fast travel** to an actor — use before `goto` across a worldspace | ⬜ |
| `look <id>` / `look off` | centre the camera on an actor, and give it back | ✅ |
| `freeze <id>` / `thaw <id>` | hold an NPC still — **always thaw** | ✅ |
| `god on` / `god off` | `Debug.SetGodMode` | ⬜ |
| `say <text>` | print into the player's console | ✅ |

`pause` blocks the **Papyrus-facing native**, which is the door addons come through. A
scheduler-side check reports success and stops nothing, because Chemistry owns the decision and asks
through the native — measured: paused at 02:42:56, Chemistry started a scene at 02:43:24.

`freeze` leaves an NPC frozen **in the save**. Put the thaw in the same command as the freeze.

### Lifecycle

| verb / script | does | |
| --- | --- | --- |
| `scratchpad/startgame.ps1` | launch via `f4se_loader.exe` | ✅ |
| `reload` | `kLoadMostRecentSave` — the newest file ON DISK | ✅ |
| `quit yes` | `Debug.QuitGame` | ⬜ |
| `scratchpad/stopgame.ps1 -Force` | kill from outside; the fallback when the mailbox is gone | ✅ |

Always launch through **`f4se_loader.exe`**, never `Fallout4.exe` — the bare exe gives a game with no
F4SE, so no Rapport, no mailbox, and a session that looks started but cannot be talked to.

`reload` works **mid-game and from the main menu**, but not at the pre-menu "Press any button to
start" splash: the game has to have accepted input once. It takes the newest save file on disk, not a
named one — **save first**, then it is a reliable reset point. It also recovers a stuck loading
screen.

**Deliberately not exposed:** `kQuickSave`, `kForceSave` and `kSaveAndQuitToDesktop` all sit in the
same enum and all write to somebody's save slots. Overwriting another person's saves is a one-way
door and not a test tool's business — which is also why `quit` goes through `Debug.QuitGame` rather
than the save-and-quit variant.

### Disabled, with the reason in the code

| verb | why |
| --- | --- |
| `passtime` | `PassTime(1)` advanced **~100 game hours** on a live save |
| `console` | no Papyrus route to the console exists; `Script` construction unsolved |

---

## Seeing the game

`shot.ps1` photographs the **Fallout 4 window**, not the desktop — the window is the useful thing and
the desktop is not ours to look at. `shot-stable.ps1` retries until the frame has content and
**settles 1500ms first**.

Three ways a screenshot lies, all of them learned the hard way:

1. **A uniform frame is a loading screen, and here it is BLUE, not black.** ~6.8 KB against 1–2.4 MB
   for a rendered world. Size is a sound test for "no content" and **cannot say which** no-content.
2. **The main menu renders a full 3D scene**, so it photographs exactly like being in the world. An
   earlier watcher called the menu `PLAYING` and sent commands at a game with no save loaded.
3. **A camera move is not instant.** A capture right after `look` photographs the rotation half-way
   through — full of real content, so every is-it-empty check passes, while showing a door instead of
   the actor. Hence `SettleMs`.

`gamestate.ps1` reports `DOWN` / `NOT-RENDERING` / `MENU` / `PLAYING` from three signals — the
process, the log's age, and a frame's size — because each alone is ambiguous. `PLAYING` requires a
recent scheduler tick, which is the only signal that actually means "there is a game to talk to".

Its state string carries **no counter**: an earlier version emitted `PLUGIN-QUIET 91s`, `92s`, `93s`
and flooded the harness, because a monitor that fires on change fires every second when the string
contains a clock.

---

## A workflow that works

```
startgame.ps1              start it
<press a key>              the splash needs one real input
reload                     known state
pause                      stop autonomy taking the slot
nearby                     who is here, with ids
request <a> <b> athome     make it happen
look <a>                   camera on them
<capture>                  see it
state <a>                  what the engine thinks
look off                   camera back
reload                     reset, go again
quit yes                   done
```

---

## What this cost to learn

Six bugs in this channel were written and found in the same session. Every one **reported success
while doing nothing**:

- `RequeueOrder` read a latch that `CallFunctionNoWait` had already overwritten
- the inbox race ate a command silently
- `pause` gated the scheduler instead of the native addons call
- `SetAngle` on the player crashed the game
- `SetCameraTarget` was inert
- `PassTime(1)` moved four game days

What caught them, every time, was **running the thing** or the owner looking at their own screen.
Never reasoning about it. That is the same shape as the original bug this channel was built to
diagnose, and it is the argument for the channel existing.

See `docs/papyrus-api-notes.md` for the API findings underneath all of this.
