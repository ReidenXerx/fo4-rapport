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

## One timer, and only one

`Rapport:Bridge` keeps a single Papyrus timer — the poll. Everything else that needs a clock asks
the plugin for the answer.

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
