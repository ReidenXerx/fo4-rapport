# Temporary changes to the reference machine

Things this project turned on outside its own repository, so they get turned off again rather than
quietly becoming permanent.

## Papyrus logging — ON, temporary (2026-09-18)

`C:\Users\DuduPhudu\Documents\My Games\Fallout4\Fallout4Custom.ini`

```ini
[Papyrus]
bEnableLogging=1      ; was 0
bEnableTrace=1        ; was 0
```

Backup: `Fallout4Custom.ini.before-rapport-debug`, same folder.

**Why:** AAF's `debug_to_papyrus_log` writes through `Debug.TraceUser`, which does nothing unless
Papyrus logging is on. Without it, AAF's worded reasons for refusing a scene went nowhere once the
pop-ups were switched off.

**Cost, and why it must not stay:** Papyrus tracing slows the script engine, on a load order of 793
plugins. That is precisely the load this mod exists to avoid adding to, so leaving it on would make
every performance measurement a lie.

**Turn it off when:** AAF's events are reaching the bridge and scene start/end are reported
reliably. Restore from the backup, or set both back to 0.

`bLoadDebugInformation` and `bEnableProfiling` were deliberately left at 0 — they are heavier again
and nothing here needs them.

## AAF settings overlay — shipped, development values

`data/AAF/Rapport_settings.ini`, deployed with the mod.

```ini
debug_to_papyrus_log    = true
troubleshooting_level   = 0
```

`troubleshooting_level` is 0 on purpose: at 2 it turns AAF's warnings into modal pop-ups, and the
owner spent a session clicking OK through six content problems in animation packs that had nothing
to do with this mod. It controls pop-ups only, never the log.

Both belong at their shipping defaults (`false` / `0`) before release.
