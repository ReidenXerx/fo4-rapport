# The script remembers, the plugin does not

Rapport is one mod in two halves with **different lifetimes**, and almost every bug worth the name
so far has come from forgetting it.

| | Lives for |
| --- | --- |
| `Rapport:Bridge` (Papyrus) | Ever. Its variables are written into the save and come back exactly as they were. |
| `Rapport.dll` (F4SE) | One session. Every launch it starts from nothing. |

A script variable meaning *"we have already done this"* is therefore a lie to a plugin that has just
been loaded for the first time.

## What it has cost, three times

- **`_ready`** persisted as `true`, so `Connect()` returned early and a freshly loaded plugin was
  never told there was a bridge. The mod worked on the session it was installed and was broken on
  every session after, for everyone.
- **`_connecting`**, a guard against a harmless startup race, persisted as `true` after a game closed
  between setting and clearing it. Every `Connect()` since returned immediately **and silently** —
  no log line, because the guard was hit before the first trace. The bridge was permanently deaf and
  looked idle.
- **AAF's own `AAF_ActorBusy`** keyword is the same shape of problem one layer out: set by
  `StartScene`, cleared only by a scene that finishes, and persisted on the actor. A request that
  died left an NPC unusable by AAF forever.

## The rule

**Session state belongs on the plugin side.** It is the only part of this mod that reliably starts
clean, which makes it the only honest answer to "have we done this yet".

`Rapport:Core.NeedsHandshake()` is that rule made concrete: the script asks the plugin whether the
two halves have been introduced *this session*, because the script's own memory cannot tell it.

Corollaries worth keeping:

- A guard that can wedge must not be more persistent than the thing it guards. `_connecting`
  protected against a duplicate log line and cost the entire event system.
- Anything that persists and blocks needs a way back. AAF's busy flag has one only because we clear
  it ourselves on failure.
- **Never guard before the first log line.** `_connecting` returned before anything was written, so
  the failure produced no output at all — the most expensive kind.

## A Papyrus stack does not come back from an AAF call

This is the one that cost three runs, because it wore two disguises first.

Every AAF API function -- `StartScene`, `StopScene`, `ApplyOverlaySet`, `ApplyMFGSet`, all of them --
ends in `AAF_MainQuestScript.sendEvent`, and that function is one line:

```papyrus
ui.Invoke("HUDMenu", SWFPath + ".receiver.sendEvent", eventData)
```

The call **delivers**: the scene starts, the overlay lands, AAF does exactly what was asked. The
stack that made the call does not continue.

The measurement, over three runs:

| Run | What happened | Polls before it stopped |
| --- | --- | --- |
| 1 | poll called `BeginRequest` -> `StartScene` | 17, last one at the `StartScene` poll |
| 2 | every `StartTimer` removed; same result | -- |
| 3 | **request 1 returned early at the busy check and SURVIVED; request 2 reached `StartScene` and did not** | 19, last one at the `StartScene` poll |

Run 3 is the one that settles it. The same function, `BeginRequest`, on two consecutive requests:
the call that returned before reaching AAF left the poll alive, and the call that reached AAF killed
it. Not the function, not the timers, not the expression layer that was blamed for two runs.

**Scheduling the next poll first does NOT fix it** -- and that attempt is what pinned the mechanism
down. If the stuck stack had merely ERRORED, the already-scheduled timer would have fired. It did
not. So the stack is alive and stuck, and **Papyrus will not start a second `OnTimer` while the
first is still running**: one stuck poll is every poll after it. `OnSceneInit` kept arriving
throughout all three runs, so different handlers DO run concurrently; it is re-entering the SAME
handler that queues.

**The fix is `CallFunctionNoWait`.** Every AAF call goes onto its own stack and `OnTimer` returns
immediately. A stack that never comes back is then a different function from the poll, and costs one
stack rather than the framework.

One correction worth keeping, because the first diagnosis was half wrong: it is **not** `ui.Invoke`
as such. `ChangeSetting` is also a `sendEvent`, and the debug hub applies two of them inside
`Connect()` without hanging. `StartScene` does more -- `makeActorData` is
`ll_fourplay.AAF_MakeActorData`, a native call into **another F4SE plugin**. Which of the two stalls
is still unproven; the design does not depend on knowing.

A consequence to design around rather than discover: one poll gets through **at most one** AAF call.
A drain loop with a budget of eight is a budget of one.

## One timer, and only one

`Rapport:Bridge` keeps a single Papyrus timer — the poll. Everything else that needs a clock asks
the plugin for the answer.

**Corrected 2026-09-23 (microscope pass 2):** the second timer id was never shown to be at fault. Run 2
below removed every `StartTimer` and failed the same way, and run 3 put it on the AAF call: a stack
that reaches AAF's `StartScene` does not come back, so the `StartTimer` after it never ran at all, and
`OnTimer` runs one at a time per script, so the stuck poll starved every other timer on it -- the third
id in `OnSceneInit` included. The rule that holds is **never call AAF on a timer's stack** (hence
`CallFunctionNoWait`); one timer stays in this script as a simplicity, not as a finding. A second id
is safe on a script whose `OnTimer` never stalls (Overture's approach keeps two).

That is not tidiness. It is measured:

> 2026-09-18. The bridge polled **17 times** — 51 seconds at a 3-second interval — and stopped at
> 20:31:52, the exact moment `BeginRequest` ran. The last statement of `BeginRequest` was
> `Self.StartTimer(afDuration + 60.0, kSceneTimer)`, a **second** timer id. The poll never came back,
> the 90-second failsafe never fired, and a `StartTimer` with a **third** id inside `OnSceneInit`
> never fired either.

Three symptoms, one cause: timer id 1 worked seventeen times, and every function that called
`StartTimer` with another id aborted at that statement.

Without the poll counter this was unfalsifiable. The poll produces **no log line when there is
nothing to do** — `Pump` is silent, `TakeRequest` returns 0, the order queue is empty — so a dead
poll and a healthy idle poll look exactly the same. Both scene timers were guarded by
`If _inFlight.Length > 0`, so their absence looked the same as "nothing to stop". Two whole runs were
spent reasoning about the wrong subsystem — the expression layer, which was never broken; it was
being starved.

`health: N poll(s), M order(s) waiting` is now on the health line, and says so loudly when N is 0.
**Any subsystem whose healthy state and broken state produce the same log needs a counter, not an
argument.**

The clock moved to where the watchdog's already was. `SceneToStop()` returns the request whose scene
has run its length, timed from `OnSceneStarted` rather than from the request — AAF walks the pair
together first, and that walk took 12.5 seconds of a 30-second scene.

## A save LOAD is a new lifetime for Papyrus but not for the plugin

The plugin keeps running across a load. The Papyrus script does not: its variables come back from
the save, and **a struct that has changed shape since that save was written comes back as `None`
rather than as an empty array**.

Measured 2026-09-18. `Struct Request` gained a `duration` field. On a second load in one session:

```
error: Cannot add elements to a None array
stack: Rapport:Bridge.BeginRequest()
       Rapport:Bridge.OnTimer()
```

`_inFlight` was `None`, `Add` failed, `BeginRequest` aborted halfway, and the request simply never
happened -- no failure reported, because the abort was *inside* the function that would have
reported it.

The reason nothing re-created the array is the asymmetry itself: `Connect()` creates it, and
`Connect()` only runs when the PLUGIN says a handshake is needed. The plugin had not restarted, so
it still believed the bridge was ready and never asked for one.

**So a load now forces a new handshake** (`RequireHandshake()` on `kPostLoadGame`), and the array is
checked at the point of use anyway. Two defences, because this is the third distinct bug caused by
the same asymmetry and the first two also looked like something else.
