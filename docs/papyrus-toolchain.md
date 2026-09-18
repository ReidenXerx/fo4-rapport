# Getting Papyrus to compile without the Creation Kit's sources

The Creation Kit installs two things this project needs and `CreationKit.exe` alone does not
carry: `Institute_Papyrus_Flags.flg`, and about 1,400 base `.psc` sources the compiler needs to
resolve `Actor`, `Quest`, `ObjectReference` and everything else. Neither existed anywhere on the
reference machine.

Both were reconstructed from the game's own files on 2026-09-18. `tools/papyrus_setup.py` does it
again from scratch.

## The flags file

Papyrus user flags are stored in every compiled script's own flag table, so the file can be read
back out of Bethesda's shipped bytecode rather than recalled:

| flag | bit |
| --- | --- |
| `hidden` | 0 |
| `conditional` | 1 |
| `default` | 2 |
| `collapsedonref` | 3 |
| `collapsedonbase` | 4 |
| `mandatory` | 5 |

`Form.pex`, `Quest.pex` and `ScriptObject.pex` each carry the same table independently, which is
three agreeing sources rather than one. The compiler accepted the reconstructed file on the first
attempt, which is the only proof that matters.

## The base sources

`Fallout4 - Misc.ba2` holds 7,875 compiled vanilla scripts and the six DLC archives hold another
2,396. All 10,271 were extracted and decompiled with [Champollion](https://github.com/Orvid/Champollion)
v1.3.2: 10,022 succeeded, and of those 381 came out as empty files that make the compiler abort
when it scans the import directory. Those are quarantined, leaving **9,641 usable sources**.

```
tools/papyrus_setup.py <champollion.exe> <output dir>
```

### The scripts that reconstruction silently drops

10,271 pex went in and 9,641 sources came out, and the gap is not only the 381 quarantined
empties. **A script that is nothing but native declarations has no bytecode, so Champollion
produces nothing at all for it** — no file, no error, no entry in the quarantine. `Utility` is
exactly that shape: `Utility.pex` was extracted with the other 10,270, no `Utility.psc` ever
appeared, and so `Utility.Wait` was simply unavailable with nothing saying why.

`tools/pex_natives.py` writes those sources from the pex directly. The function table carries the
return type, the parameter names and their types, and the global/native flags, so each declaration
is read out of the thing that defines it rather than transcribed from a wiki. It refuses rather
than guesses: if a function turns out to have instructions it stops, and if the parse does not
consume the file to its last byte it writes nothing.

```
python tools/pex_natives.py <in.pex> [out.psc]
```

Two things that cost an hour and are worth writing down:

- The Fallout 4 object header has **a `u8` between the doc string and the user flags** that the
  Skyrim layout does not. Get it wrong and the parse survives the header, drifts, and dies hundreds
  of bytes later with a nonsense error. Its position was settled by evidence rather than assumed:
  reading it *before* the doc string also consumes all 10,271 vanilla scripts without complaint,
  because the doc string is almost always empty and the byte almost always zero. On the scripts
  where the two readings differ, the wrong one reports every script's doc string as that script's
  own name.
- **`UI` is F4SE's, not Bethesda's** — its pex header names `ianpatt` and an f4se build path — which
  is why it appears in no vanilla BA2. It needs no reconstruction at all: F4SE ships the real
  source at `Data/Scripts/Source/UI.psc`, comments, default arguments and all. Copy it.

`UI.IsMenuOpen` is worth knowing for its own sake. `UI.Load` puts an asset inside a menu that must
already exist, so anything driving Scaleform — AAF's entire event channel, and every restart of it
— silently does nothing when that menu is absent.

## The one thing this costs you

**Decompiled sources lose every default argument value.** Papyrus compiles a default into the
*caller*, not the callee, so the value is simply not present in the bytecode for any decompiler to
recover. `Debug.psc` comes back as:

```papyrus
Function Trace(String asTextToPrint, Int aiSeverity) Global Native     ; real: aiSeverity = 0
```

Not one decompiled base script carries a default. Every call into a vanilla function must therefore
pass **all** of its arguments explicitly:

```papyrus
Debug.Trace("...")        ; argument aiseverity is not specified and has no default value
Debug.Trace("...", 0)     ; compiles
```

That is a compile error rather than a silent one, so nothing can slip through unnoticed — but where
we do not know a default, passing the wrong value changes behaviour quietly. When a default's real
value matters, read it from a *caller's* bytecode: the compiler baked the literal into every vanilla
script that relied on it, and `tools/pexnames.py` plus the readers in `tools/` can find it.

Getting the Creation Kit's actual `Data/Scripts/Source/Base` removes this entire class of problem.
It is worth swapping to if the package ever turns up.

## The other thing it costs you: CustomEvent declarations

Champollion does not recover `CustomEvent` declarations either, and a script that sends a custom
event must declare it. Registering for one of AAF's events against a decompiled `AAF_API.psc`
therefore fails with:

```
aaf:aaf_api.onsceneend does not exist because aaf:aaf_api cannot generate onsceneend events
```

The names are still recoverable from the decompiled source, because every send carries the event
name as a literal: `SendCustomEvent("aaf:aaf_api_OnSceneEnd", akArgs)` — the script name lowercased,
colon intact, then the event. `tools/restore_custom_events.py` reads those and writes the missing
declarations back. It restored 16 on `AAF_API` and 17 on `AAF_MainQuestScript`, and it is
idempotent, so re-running after a fresh decompile is safe.

## The third thing it costs you: custom event names are mangled at COMPILE time

This one is invisible, and it silently breaks every custom event you try to receive.

Papyrus rewrites a custom event name at compile time to `<declaring script, lowercased>_<Event>`.
AAF's shipped `AAF_API.pex` therefore contains the literal `aaf:aaf_api_OnSceneInit`, and the VM
matches registrations against senders **by that string**.

Compiled against decompiled sources with hand-restored `CustomEvent` declarations, the mangling does
not happen. Proven by compiling both forms and reading the string table back:

```papyrus
RegisterForCustomEvent(api, "OnSceneInit")             ; emits "OnSceneInit"            -- never matches
RegisterForCustomEvent(api, "aaf:aaf_api_OnSceneInit") ; emits "aaf:aaf_api_OnSceneInit" -- matches
```

The restored declarations are enough for `Event AAF:AAF_API.OnSceneInit(...)` to compile, so
everything *looks* right: the script builds, registers, and receives nothing. Ever. There is no
error, because `RegisterForCustomEvent` returns void.

**So register with the name the sender actually sends**, taken from its own string table rather than
from its source. `tools/pexnames.py` and the string-table reader will show it.

## Compiling a namespaced script

`Rapport:Bridge` lives at `papyrus/Rapport/Bridge.psc`, and compiling it *by path* fails with
"filename does not match script name": the namespace comes from the import paths, not the file. Use
batch mode over the directory, or name the script:

```
PapyrusCompiler.exe <papyrus dir> -all -f="Institute_Papyrus_Flags.flg" -i="<base>;<papyrus dir>" -o="<out>"
PapyrusCompiler.exe "Rapport:Core"  -f="Institute_Papyrus_Flags.flg" -i="<base>;<papyrus dir>" -o="<out>"
```

Also note `Native` is a reserved word: a script cannot be called `Rapport:Native`, which is why the
native declarations live in `Rapport:Core`.

## Compiling

```
PapyrusCompiler.exe <script.psc> -f="Institute_Papyrus_Flags.flg" -i="<base dir>" -o="<out dir>"
```

`scripts/build-papyrus.ps1` wraps that for this project. The base sources live outside the game
folder on purpose — the compiler takes import paths, so Vortex never sees them and nothing is
deployed that should not be.

Never compile into `Data/Scripts`: that directory is Vortex-deployed.
