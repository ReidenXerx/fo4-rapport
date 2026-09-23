"""Generate Rapport's MCM menu from the files that already hold the defaults.

    python scripts/build-mcm.py

Writes data/MCM/Config/Rapport/config.json (the pages) and settings.ini (what MCM
shows as the default). The defaults are READ from scoring.json and barks.json, never
typed here, so the menu cannot drift from what the plugin does without MCM: there is
one source for every number. Re-run after changing either json.

The plugin reads the player's choices from Data/MCM/Settings/Rapport.ini under the
same section and key names (src/McmSettings.cpp), and reloads within one 20s pass
of a slider moving.
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "data" / "F4SE" / "Plugins" / "Rapport"
OUT = ROOT / "data" / "MCM" / "Config" / "Rapport"

scoring = json.loads((DATA / "scoring.json").read_text(encoding="utf-8"))
barks = json.loads((DATA / "barks.json").read_text(encoding="utf-8"))
observers = barks["observers"]
narrator = json.loads((DATA / "narrator.json").read_text(encoding="utf-8"))

# (section, key, source, label, help, min, max, step)  - step None = switch
PAGES = [
    ("Scene choice", [
        ("text", "How Rapport scores a pair. With Chemistry installed, Chemistry makes the decision and adds its "
                 "own bonuses on top of this score; without it, Rapport's own trigger starts a scene when the best "
                 "pair clears the bar."),
        ("section", "The bar"),
        ("Scoring", "minimumScore", scoring, "Score needed (Rapport's own trigger)",
         "The score the best pair must reach before Rapport's built-in trigger starts a scene. Chemistry has its "
         "own bar and ignores this one.", 0.0, 5.0, 0.05),
        ("section", "Who counts as a pair"),
        ("Scoring", "maxPairDistance", scoring, "Furthest apart", "Two people further apart than this (game units) "
         "are never paired.", 256, 4096, 64),
        ("Scoring", "proximity", scoring, "Closeness weight", "What standing close together is worth. It falls "
         "off with the square of distance.", 0.0, 3.0, 0.05),
        ("Scoring", "sharedFaction", scoring, "Same faction", "Bonus when both belong to a shared faction.",
         0.0, 2.0, 0.05),
        ("Scoring", "interior", scoring, "Indoors", "Bonus for being indoors.", 0.0, 2.0, 0.05),
        ("Scoring", "night", scoring, "Night", "Bonus at night.", 0.0, 2.0, 0.05),
        ("section", "Privacy"),
        ("Scoring", "observerRadius", scoring, "Onlooker radius", "Anyone within this distance of the pair "
         "counts as watching.", 300, 3000, 50),
        ("Scoring", "observerTolerance", scoring, "Free onlookers", "Onlookers who cost nothing. The crowd "
         "penalty starts after this many. Chemistry uses the same number.", 0, 10, 1),
        ("Scoring", "perObserver", scoring, "Crowd penalty", "Cost per onlooker past the free ones, before the "
         "curve below.", 0.0, 1.0, 0.05),
        ("Scoring", "observerFalloff", scoring, "Crowd curve", "How steeply the crowd penalty climbs: 1 is a "
         "straight line, higher punishes big crowds harder.", 1.0, 3.0, 0.1),
        ("Scoring", "playerNear", scoring, "Player watching", "Cost when the player is nearby. 0 by default: "
         "the player is not a factor.", 0.0, 2.0, 0.05),
    ]),
    ("Voices", [
        ("section", "Scene lines"),
        ("Barks", "enabled", barks, "Pair lines", "The two in a scene say something as it starts.", None, None, None),
        ("Barks", "responderDelaySeconds", barks, "Answer after (s)", "How long the second one waits before "
         "answering.", 1.0, 15.0, 0.5),
        ("section", "Bystanders"),
        ("Observers", "enabled", observers, "Bystander comments", "People who see or hear a scene turn their "
         "head and may comment.", None, None, None),
        ("Observers", "radius", observers, "Notice radius", "How far away a bystander can notice a scene.",
         200, 3000, 50),
        ("Observers", "hearRadius", observers, "Hearing radius", "Within this distance a bystander hears it "
         "through walls. Never more than the notice radius.", 0, 3000, 50),
        ("Observers", "chance", observers, "Chance to comment", "Each bystander who notices rolls once per "
         "scene.", 0.0, 1.0, 0.05),
        ("Observers", "startAfterSeconds", observers, "Start after (s)", "No comments in the first seconds of "
         "a scene.", 0, 120, 1),
        ("Observers", "gapSeconds", observers, "Gap between comments (s)", "So two bystanders never talk over "
         "each other.", 0, 60, 1),
        ("Observers", "cooldownSeconds", observers, "Per-person cooldown (s)", "How long before the same "
         "bystander comments again.", 0, 1800, 30),
    ]),
    ("Narrator", [
        ("text", "A line on the HUD saying who is about to have a scene and why, then the numbers behind it. "
                 "It covers every mod that uses Rapport, anywhere in the loaded area."),
        ("section", "Narrator"),
        ("Narrator", "enabled", narrator, "Narrator", "Show narration at all.", None, None, None),
        ("Narrator", "numbers", narrator, "Show the numbers", "A second line with the score, part by part.",
         None, None, None),
        ("section", "What to narrate"),
        ("Narrator", "sceneStarts", narrator, "A scene starts", "Who, and why now.", None, None, None),
        ("Narrator", "nearMisses", narrator, "Why nothing happened", "Now and then, a likely pair that was passed "
         "over and why. Rate-limited.", None, None, None),
        ("Narrator", "relationshipTurns", narrator, "Relationship turns", "A first time together, or a bond "
         "crossing a line: getting close, close, inseparable, fallen out.", None, None, None),
        ("Narrator", "bystanders", narrator, "Bystanders react", "Someone noticed, or heard.", None, None, None),
        ("section", "History"),
        ("button", "Show recent narration", "The last entries, with the time in the world.",
         {"type": "CallGlobalFunction", "script": "Rapport:Narrator", "function": "ShowHistory", "params": []}),
        ("section", "Pacing"),
        ("Narrator", "nearMissCooldownSeconds", narrator, "Between 'why not' lines (s)", "At most one near miss "
         "this often, across all pairs.", 30, 1800, 30),
        ("Narrator", "pairMissCooldownSeconds", narrator, "Same pair again after (s)", "How long before the same "
         "two are narrated as a near miss again.", 60, 7200, 60),
    ]),
]


def mcm_id(key, kind):
    """MCM reads a setting's TYPE from the FIRST LETTER of its id.

    i = int, f = float, b = bool, s = string. A plain word registers as nothing,
    and MCM says so once per setting in MCM.log and then carries on -- which is
    why Rapport shipped a menu where 26 of 28 controls could not store a value
    and nobody noticed. The two that worked were accidents: "sharedFaction"
    registered as a STRING and "interior" as an INT, both wrong.

    The C++ that reads the player's ini strips this prefix again, so a player who
    already changed a setting under the old bare key keeps their value.
    """
    return kind + key[0].upper() + key[1:]


def fmt(value):
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    return f"{float(value):.6f}"


pages, ini = [], {}
for title, rows in PAGES:
    content = []
    for row in rows:
        if row[0] == "text":
            content.append({"type": "text", "text": row[1]})
            continue
        if row[0] == "section":
            content.append({"type": "section", "text": row[1]})
            continue
        if row[0] == "button":
            content.append({"type": "button", "text": row[1], "help": row[2], "action": row[3]})
            continue
        section, key, source, label, help_, lo, hi, step = row
        if key not in source:
            raise SystemExit(f"{key} is not in the json it is supposed to come from - the menu would lie")
        value = source[key]
        ini.setdefault(section, {})[key] = fmt(value)
        if step is None:
            content.append({"type": "switcher", "id": f"{mcm_id(key, 'b')}:{section}",
                            "text": label, "help": help_,
                            "valueOptions": {"sourceType": "ModSettingBool"}})
            continue
        integral = all(isinstance(v, int) and not isinstance(v, bool) for v in (lo, hi, step)) and \
            isinstance(value, int)
        content.append({"type": "slider", "id": f"{mcm_id(key, 'i' if integral else 'f')}:{section}",
                        "text": label, "help": help_,
                        "valueOptions": {"min": lo, "max": hi, "step": step,
                                         "sourceType": "ModSettingInt" if integral else "ModSettingFloat"}})
    pages.append({"pageDisplayName": title, "content": content})

config = {"modName": "Rapport", "displayName": "Rapport", "minMcmVersion": 1,
          "pluginRequirements": ["Rapport.esp"], "pages": pages}

OUT.mkdir(parents=True, exist_ok=True)
(OUT / "config.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
lines = ["; GENERATED by scripts/build-mcm.py from scoring.json, barks.json and narrator.json - edit those, not this."]
for section, keys in ini.items():
    lines.append(f"[{section}]")
    lines += [f"{k}={v}" for k, v in keys.items()]
    lines.append("")
(OUT / "settings.ini").write_text("\n".join(lines), encoding="utf-8")
print(f"wrote {OUT / 'config.json'} and settings.ini: "
      f"{sum(len(k) for k in ini.values())} settings across {len(pages)} pages")
