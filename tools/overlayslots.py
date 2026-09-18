"""Every installed LooksMenu overlay, by SLOT.

Answers one question: does any installed mod put an overlay on the HEAD? On this
machine the answer was no -- 959 templates across 14 mods, 925 on slot 3 (body)
and 34 on slot 4 (hands), none anywhere else. That is why Rapport_Oral lands on
the chest.

Re-run it after installing any overlay pack. A pack that ships a head-slot
template is one Rapport can drive; a pack that does not is one that cannot help,
whatever its description says.
"""
import json
import pathlib
import re

root = pathlib.Path(r"D:\GOGGames\Fallout 4 GOTY\Data\F4SE\Plugins\F4EE\Overlays")

slots = {}
face_templates = []

for folder in sorted(root.iterdir()):
    js = folder / "overlays.json"
    if not js.is_file():
        continue
    try:
        data = json.loads(js.read_text(encoding='utf-8-sig'))
    except Exception as e:
        print("  !! %s: %s" % (folder.name, e))
        continue

    per_mod = {}
    for t in data:
        for s in t.get('slots', []):
            slot = s.get('slot')
            per_mod[slot] = per_mod.get(slot, 0) + 1
            slots[slot] = slots.get(slot, 0) + 1
            if slot != 3:
                face_templates.append((folder.name, t.get('id'), slot, s.get('material', '')))
    print("%-55s %s" % (folder.name, per_mod))

print()
print("slot totals across every installed overlay mod:", slots)
print()
print("templates NOT on slot 3 (the body slot):")
for mod, tid, slot, mat in face_templates:
    print("   slot %-3s %-40s %-28s %s" % (slot, tid, mod, mat))
if not face_templates:
    print("   none - every installed overlay template is body-slot")
