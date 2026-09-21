# The scripted demo

Recording a demo of Rapport should not mean waiting for autonomy to pick a pair near the camera.
These scripts travel to a location, frame the shot, start a scene, keep the camera on it, record it
with OBS, and narrate everything that happens — every line with its text, who noticed, who reacted,
and whether they saw it or heard it.

**Scenes are scripted; what people say is not.** The lines, the personas and which bystanders react
are the mod's own choices, which is the point of showing it.

## Once

```
python scripts/demo/record.py setup     # launches OBS if needed; scene, Game Capture, desktop audio, 1080p60, MKV
```

OBS's websocket password lives in `~/.obs-websocket/password` and is never printed. Setup mutes every
microphone input: the first clips carried room noise under the voice lines. On a fresh OBS,
close its first-run **Auto-Configuration Wizard** (and any update prompt) once — while either is open,
OBS accepts "start recording" and silently records nothing.

## Every session

```
python scripts/demo/demo.py prepare     # DEMO MODE - game CLOSED (deploy-dev.ps1 also resets it)
# launch the game through F4SE, load the save
python scripts/demo/demo.py list
python scripts/demo/demo.py run city-hall
python scripts/demo/demo.py run all
python scripts/demo/demo.py restore     # shipped config - game CLOSED
```

Demo mode makes every bystander who sees or hears a scene react (chance 100%, 30s cooldown, 5s
between lines). Shipped values: 33%, 300s, 6s. Clips land in `Videos/Rapport demo/`, one per shot,
named by shot. `--no-record` runs a shot without OBS.

## The shots

| Shot | Where | What it shows |
| --- | --- | --- |
| `city-hall` | Diamond City, City Hall | opener + answer, subtitled; Geneva's unique voice borrowed; McDonough behind a wall hears it |
| `dugout-inn` | Diamond City, the Dugout Inn | a bar crowd: heads turn, patrons comment in turn |
| `third-rail` | Goodneighbor, the Third Rail | another worldspace, a crowd, a tender scene |
| `state-house` | Goodneighbor, the Old State House | two unique voices (Hancock, Fahrenheit) through the voice fallback |

No location has children nearby. The watchers skip children anyway; a demo of this mod is still not
filmed where they stand around, which is why Diamond City's market is not a shot.

## Cameraman mode - the owner films

```
python scripts/demo/demo.py run dugout-inn --cameraman
```

Only triggers the events: pauses autonomy, starts the scene, narrates it, records it. No travel, no
framing, no camera lock, no follow - the owner walks there and holds the camera. Standing still
while the script pinned the view let Fallout 4's idle camera start orbiting the player, which is
why this mode exists. If the pair is not near the player it says where to go and starts nothing.

## The camera (scripted mode)

- **Framing** is `watch <actor> <distance> move`: always repositions, always facing them, and only
  ever locally.
- **Fixed gaze**: `look <actor>` locks the camera on the pair until `look off` at the end of the shot.
- **Follow**: every 3 seconds the runner measures the distance to the pair and re-frames if they
  have drifted more than 150 units from the shot.

## Safety — each rule is a crash or a hang this project has already had

- **Nothing in a demo MoveTos while a scene can be running.** The first version re-framed with
  `goto`; mid-scene the guard stood in an *adjacent* exterior cell of Diamond City, `goto` took the
  MoveTo path, and the loading screen wedged for good. `goto` and `watch` now treat the same cell, or
  the same worldspace within 3000 units, as local; `watch ... move` refuses rather than MoveTo.
- **Never travel while a scene is starting**: travel inside AAF's scene-init window crashed the game
  75% of the time. A shot waits for its scene to END before the next travel, and re-frames only 3
  seconds after the scene has started.
- **Across cells and worldspaces, `travel`** (fast travel), never a plain move.
- **No AAF call during a loading screen** (fixed in the bridge, not the demo): a fast travel 22s after
  a scene met the aftermath's overlay removal, a `ui.Invoke` into AAF's torn-down Flash interface, and
  the game crashed in Scaleform. The bridge now holds every order until the load is over.
- **A loading screen is timed by the channel's silence**, because the game answers nothing while it
  loads.
