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

## Compiling

```
PapyrusCompiler.exe <script.psc> -f="Institute_Papyrus_Flags.flg" -i="<base dir>" -o="<out dir>"
```

`scripts/build-papyrus.ps1` wraps that for this project. The base sources live outside the game
folder on purpose — the compiler takes import paths, so Vortex never sees them and nothing is
deployed that should not be.

Never compile into `Data/Scripts`: that directory is Vortex-deployed.
