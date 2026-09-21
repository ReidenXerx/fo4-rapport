#!/usr/bin/env python3
"""
The scripted demo: travel to a location, frame the shot, start a scene, and
narrate everything that happens - so recording a video is not waiting for luck.

    python scripts/demo/demo.py list                 # the shots
    python scripts/demo/demo.py prepare              # DEMO MODE config (game must be CLOSED)
    python scripts/demo/demo.py run city-hall        # one shot
    python scripts/demo/demo.py run all              # every shot, in order
    python scripts/demo/demo.py restore              # back to the shipped config (game CLOSED)

Scenes are scripted; what people SAY is not - lines, personas and who among
the bystanders reacts are the mod's own choices, which is the point of showing
it. Demo mode only makes reactions reliable enough to film: every bystander who
sees or hears it wins their roll, and the cooldown is short enough for retakes.

Needs a dev build (the command channel is on in deploy-dev staging) and a save
loaded in game. Talks to Rapport's channel (Rapport.cmd / Rapport.cmd.out) and
reads Rapport.log for the narration.

SAFETY, each rule a measured crash or hang in this project:
  - never travel while a scene is starting: travel inside AAF's scene-init window
    crashed the game 75% of the time. A shot waits for its scene to END;
  - across cells and worldspaces, FAST TRAVEL (travel), never a plain move: a
    MoveTo across a worldspace leaves the loading screen up forever;
  - a loading screen is timed by the channel's SILENCE, because the game does
    not answer anything while it loads.
"""
import argparse
import json
import pathlib
import re
import shutil
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
SHOTS = json.loads((pathlib.Path(__file__).parent / "shots.json").read_text(encoding="utf-8"))["shots"]
F4SE = pathlib.Path.home() / "Documents/My Games/Fallout4/F4SE"
CMD, OUT, LOG = F4SE / "Rapport.cmd", F4SE / "Rapport.cmd.out", F4SE / "Rapport.log"
STAGED = pathlib.Path(r"D:\Vortex\fallout4\mods\Rapport-dev\F4SE\Plugins\Rapport\barks.json")
SHIPPED = ROOT / "data/F4SE/Plugins/Rapport/barks.json"
LINES = {l["id"]: l["text"] for l in json.loads((ROOT / "voice/lines.json").read_text(encoding="utf-8"))["lines"]}

MCP_CMD, MCP_OUT = F4SE / "F4MCP.cmd", F4SE / "F4MCP.cmd.out"
sys.path.insert(0, str(pathlib.Path(__file__).parent))

DEMO = {"chance": 1.0, "cooldownSeconds": 30, "gapSeconds": 5, "startAfterSeconds": 8}


def lines(path):
    return path.read_text(encoding="utf-8", errors="replace").splitlines() if path.exists() else []


def send(verb, timeout=12.0):
    """One verb to Rapport's channel; the reply line(s) that follow it."""
    mark = len(lines(OUT))
    with CMD.open("a", encoding="utf-8") as fh:
        fh.write(verb + "\n")
    end = time.time() + timeout
    while time.time() < end:
        new = lines(OUT)[mark:]
        replies = [l for l in new if l.startswith(("OK", "ERR"))]
        if replies:
            return replies[0]
        time.sleep(0.2)
    return None


def distance_to(ref):
    """Player -> actor distance in units, from fo4-mcp's `nearby`, or None."""
    mark = len(lines(MCP_OUT))
    with MCP_CMD.open("a", encoding="utf-8") as fh:
        fh.write("nearby 3000\n")
    end = time.time() + 6
    while time.time() < end:
        new = lines(MCP_OUT)[mark:]
        if any("<<END>>" in l for l in new):
            for l in new:
                m = re.match(r"\s*" + ref.upper() + r"\s+(\d+)u", l)
                if m:
                    return float(m.group(1))
            return None
        time.sleep(0.2)
    return None


def game_running():
    import subprocess
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Fallout4.exe"], capture_output=True, text=True).stdout
    return "Fallout4.exe" in out


def wait_for_load(settle=8.0, give_up=240.0):
    """A loading screen is SILENCE on the channel; over when pings answer for `settle` seconds."""
    start, quiet_since = time.time(), None
    while time.time() - start < give_up:
        ok = send("ping", timeout=2.5)
        if ok and ok.startswith("OK"):
            quiet_since = quiet_since or time.time()
            if time.time() - quiet_since >= settle:
                return True
        else:
            quiet_since = None
        time.sleep(0.5)
    return False


def say(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def narrate(line):
    """A Rapport.log line -> a readable event, or None."""
    m = re.search(r"request \d+: bark - (\S+) \((\w+)\) opens with ([^,\s]+)", line)
    if m:
        return f"OPENS    {m.group(1)} ({m.group(2)}): \"{LINES.get(m.group(3), m.group(3))}\""
    m = re.search(r"request \d+: bark - (\S+) answers with (\S+)", line)
    if m:
        return f"ANSWERS  {m.group(1)}: \"{LINES.get(m.group(2), m.group(2))}\""
    m = re.search(r"voices: (\S+) has a voice we did not render - borrowing (\S+)", line)
    if m:
        return f"VOICE    {m.group(1)} borrows voice {m.group(2)} for that line"
    m = re.search(r"watcher (\S+) noticed it \(([^)]*)\)", line)
    if m:
        return f"NOTICES  {m.group(1)} turns their head ({m.group(2)})"
    m = re.search(r"watcher (\S+) \((\w+), (\w+), (heard it|saw it)\) says (\S+)", line)
    if m:
        return f"REACTS   {m.group(1)} ({m.group(2)}, {m.group(3)}, {m.group(4)}): \"{LINES.get(m.group(5), m.group(5))}\""
    m = re.search(r"watcher (\S+) noticed it but never (.*)", line)
    if m:
        return f"SILENT   {m.group(1)} noticed, but never {m.group(2)}"
    if "scene ended" in line:
        return "ENDS     scene ended"
    return None


def run_shot(shot, max_scene, record=True):
    say(f"== {shot['name']}: {shot['where']}")
    say(f"   shows: {shot['shows']}")
    reply = send("pause")
    say(f"   autonomy: {reply}")

    first, second = shot["pair"]
    here = send(f"who {first}")
    if not (here and "loaded=yes" in here):
        say(f"   travelling to {shot['travel']} (fast travel - never a plain move across cells)")
        say(f"   {send('travel ' + shot['travel'], timeout=20)}")
        time.sleep(3)
        if not wait_for_load():
            say("   STOPPED: the game never came back from the load")
            return False
        say("   loaded")
    for who in shot["pair"]:
        r = send(f"who {who}")
        if not (r and "loaded=yes" in r):
            say(f"   STOPPED: {who} is not here ({r}) - nothing was started")
            return False

    frame(shot["frame"]["ref"], shot["frame"]["distance"], "framing")
    if record:
        import record as rec
        rec.start(shot["name"])

    mark = len(lines(LOG))
    r = send(f"request {first} {second} {shot['scenario']}")
    say(f"   request: {r}")
    if not (r and r.startswith("OK")):
        return False
    say(f"   resume: {send('resume')}")
    say("   waiting for the scene (AAF walks the pair together first)...")

    start, started, seen, last_follow = time.time(), False, 0, 0.0
    while time.time() - start < max_scene:
        new = lines(LOG)[mark:]
        for l in new[seen:]:
            if "scene started" in l:
                started = True
                say("STARTS   scene started")
                # AAF walked the pair to the animation spot, so the first frame is
                # stale. Wait out AAF's scene-init window before moving the player
                # (travel inside it crashed the game), then frame where they ARE.
                time.sleep(3)
                frame(first, shot["frame"]["distance"], "reframing on the scene")
            if "refused" in l.lower() and "request" in l:
                say(f"REFUSED  {l.split('] ', 2)[-1][:140]}")
            ev = narrate(l)
            if ev:
                say(ev)
            if "scene ended" in l:
                send("pause")
                time.sleep(2)
                send("look off")
                if record:
                    import record as rec
                    say(f"   clip: {rec.stop()}")
                say("   shot done - autonomy held")
                return True
        seen = len(new)
        # FOLLOW: if the pair has moved off the framing, frame them again -
        # locally, never by MoveTo (watch ... move refuses rather than wedge).
        if started and time.time() - last_follow >= 3.0:
            last_follow = time.time()
            d = distance_to(first)
            if d is not None and abs(d - shot["frame"]["distance"]) > 150:
                frame(first, shot["frame"]["distance"], f"following (they were {d:.0f} units away)")
        time.sleep(0.5)
    say(f"   {'scene still running' if started else 'no scene started'} after {max_scene:.0f}s - "
        "leaving it; NOT travelling until it ends")
    send("pause")
    send("look off")
    if record:
        import record as rec
        say(f"   clip: {rec.stop()}")
    return started


def frame(ref, distance, why):
    """Put the camera `distance` units from `ref`, facing them - moving the player LOCALLY.

    `watch ... move` always repositions, and never by MoveTo: if the target is not
    local it refuses. The first version used `goto` here, and mid-scene the guard
    sat in an ADJACENT exterior cell of Diamond City, so goto took the MoveTo path
    and wedged the loading screen for good. Nothing in a demo may MoveTo while a
    scene can be running.
    """
    say(f"   {why}: {send(f'watch {ref} {distance} move')}")
    time.sleep(1.5)
    # FIXED GAZE: lock the camera on them (Game.SetCameraTarget) until `look off`.
    send(f"look {ref}")


def set_config(values):
    if game_running():
        sys.exit("the game is running - close it first: the config is read at game start")
    cfg = json.loads(STAGED.read_text(encoding="utf-8"))
    cfg["observers"] = {**cfg.get("observers", {}), **values}
    # In place: the staging file is hardlinked into the game's Data folder.
    STAGED.write_text(json.dumps(cfg, indent=1) + "\n", encoding="utf-8")
    print("observers now:", cfg["observers"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["list", "prepare", "run", "restore"])
    ap.add_argument("shot", nargs="?")
    ap.add_argument("--max-scene", type=float, default=240.0, help="seconds to wait for a scene to end")
    ap.add_argument("--no-record", action="store_true", help="do not drive OBS")
    a = ap.parse_args()

    if a.what == "list":
        for s in SHOTS:
            print(f"{s['name']:12s} {s['where']}\n{'':12s} shows: {s['shows']}")
        return 0
    if a.what == "prepare":
        set_config(DEMO)
        print("DEMO MODE: every bystander who sees or hears a scene reacts. `restore` when done.")
        return 0
    if a.what == "restore":
        shipped = json.loads(SHIPPED.read_text(encoding="utf-8"))["observers"]
        set_config(shipped)
        return 0

    if not game_running():
        sys.exit("start the game and load a save first")
    todo = SHOTS if a.shot in (None, "all") else [s for s in SHOTS if s["name"] == a.shot]
    if not todo:
        sys.exit(f"no shot called {a.shot!r} - try: list")
    for shot in todo:
        if not run_shot(shot, a.max_scene, record=not a.no_record):
            say(f"== {shot['name']} did not complete - stopping here")
            return 1
    say("== demo complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
