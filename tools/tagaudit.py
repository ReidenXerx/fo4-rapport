"""Which act tags the installed packs use that aftermath.json does not cover.

A tag we do not know is indistinguishable, in the log, from a scene that was only
kissing -- both come out as "nothing to leave behind". This says which is which.

    python tools/tagaudit.py [path to Data/AAF]
"""
import collections
import glob
import io
import json
import os
import re
import sys

AAF = sys.argv[1] if len(sys.argv) > 1 else r"D:\GOGGames\Fallout 4 GOTY\Data\AAF"
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RULES = os.path.join(HERE, "data", "F4SE", "Plugins", "Rapport", "aftermath.json")

# A tag naming a sex act, rather than a mood, a piece of furniture or a pack name.
ACTISH = re.compile(
    r"(penis|vagina|anus|anal|mouth|oral|tongue|nipple|breast|blowjob|rimjob|"
    r"cunnilingus|analingus|handjob|fisting|masturbat|strapon|dildo|climax|"
    r"creampie|facial|titfuck|footjob|deepthroat|69)", re.I)


def tags_in(folder):
    counts = collections.Counter()
    for path in glob.glob(os.path.join(folder, "*.xml")):
        try:
            text = io.open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for blob in re.findall(r'tags="([^"]*)"', text):
            for tag in blob.split(","):
                tag = tag.strip()
                if tag:
                    counts[tag] += 1
    return counts


def main():
    counts = tags_in(AAF)
    if not counts:
        print("no tags found under", AAF)
        return 1

    known = set()
    with io.open(RULES, encoding="utf-8") as fh:
        for tag in json.load(fh).get("tags", {}):
            known.add(tag.lower())

    covered, missing = [], []
    for tag, n in counts.items():
        if not ACTISH.search(tag):
            continue
        (covered if tag.lower() in known else missing).append((n, tag))

    covered.sort(reverse=True)
    missing.sort(reverse=True)

    print("%d distinct tags in use; %d look like acts" % (len(counts), len(covered) + len(missing)))
    print("  covered by aftermath.json: %d" % len(covered))
    print("  NOT covered:               %d" % len(missing))
    print()
    print("=== act-looking tags with NO rule (count, tag) ===")
    for n, tag in missing:
        print("  %6d  %s" % (n, tag))
    print()
    print("=== rules that match nothing installed (dead entries) ===")
    used = {t.lower() for t in counts}
    for tag in sorted(known):
        if tag not in used:
            print("        %s" % tag)
    return 0


sys.exit(main())
