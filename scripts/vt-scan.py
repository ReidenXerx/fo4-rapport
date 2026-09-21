#!/usr/bin/env python3
"""
Check a file against VirusTotal. Look up by hash first; upload only on request.

    python scripts/vt-scan.py build/Release/Rapport.dll            # lookup only
    python scripts/vt-scan.py build/Release/Rapport.dll --upload   # submit if unknown
    python scripts/vt-scan.py <sha256> --hash                      # lookup a bare hash

WHY LOOKUP AND UPLOAD ARE SEPARATE FLAGS.

A VirusTotal result is a PERMANENT PUBLIC RECORD. Anyone can look up that hash
forever and you cannot delete or retract a scan. That is excellent when it comes
back clean and a liability when it does not: F4SE plugins hook a running process,
which is exactly what malware looks like to a heuristic, and a page reading
"2 security vendors flagged this file" is a citation you handed to whoever was
already arguing with you.

So a lookup is free and creates nothing. An upload is a one-way door and needs
--upload, deliberately, every time.

Key: VT_API_KEY, or the first line of ~/.virustotal/api-key. Never printed.
Free tier is 4 requests/minute and 500/day, which is ample for release checks.
"""
import argparse
import hashlib
import json
import os
import pathlib
import sys
import time
import urllib.error
import urllib.request

API = "https://www.virustotal.com/api/v3"


def api_key() -> str:
    k = os.environ.get("VT_API_KEY", "").strip()
    if k:
        return k
    f = pathlib.Path.home() / ".virustotal" / "api-key"
    if not f.exists():
        sys.exit("no VT_API_KEY and no ~/.virustotal/api-key\n"
                 "  get one at https://www.virustotal.com/gui/my-apikey")
    return f.read_text(encoding="utf-8").splitlines()[0].strip()


def get(path, key):
    req = urllib.request.Request(API + path, headers={"x-apikey": key})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        sys.exit(f"HTTP {e.code}: {e.read()[:300].decode(errors='replace')}")


def write_badge(path, stats, sha, version=None):
    """Write a shields.io endpoint JSON so the mod page badge is LIVE.

    A hardcoded badge asserts a number forever. Engines update their signatures,
    so a file that is 0/74 today can be 1/74 next month, and a badge that cannot
    change would then be a false claim sitting on the mod page - which is exactly
    the kind of unverifiable assurance this whole pipeline exists to replace.

    This is read by https://img.shields.io/endpoint?url=<raw url of this file>,
    so re-running the scan updates the page without editing the page.
    """
    mal = stats.get("malicious", 0) + stats.get("suspicious", 0)
    total = sum(v for v in stats.values() if isinstance(v, int))
    badge = {
        "schemaVersion": 1,
        "label": "VirusTotal",
        # The VERSION goes in the badge. A scan is a fact about one build; a badge
        # that only says "0/74 clean" silently becomes a claim about an old file
        # the day a new version ships. With the version on it, staleness is
        # visible instead of false.
        "message": ((f"{version} · " if version else "")
                    + (f"{mal}/{total} flagged" if mal else f"0/{total} clean")),
        "color": "red" if mal else "brightgreen",
        "isError": bool(mal),
    }
    p = pathlib.Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps({**badge, "_sha256": sha}, indent=2), encoding="utf-8")
    print(f"  badge -> {p}  ({badge['message']})")


def report(data, sha, badge_path=None, version=None):
    attrs = data["data"]["attributes"]
    stats = attrs.get("last_analysis_stats", {})
    mal = stats.get("malicious", 0)
    sus = stats.get("suspicious", 0)
    total = sum(v for v in stats.values() if isinstance(v, int))
    print(f"  engines     : {total}")
    print(f"  malicious   : {mal}")
    print(f"  suspicious  : {sus}")
    print(f"  harmless    : {stats.get('harmless', 0)}")
    print(f"  undetected  : {stats.get('undetected', 0)}")
    if mal or sus:
        print("\n  flagged by:")
        for eng, res in sorted((attrs.get("last_analysis_results") or {}).items()):
            if res.get("category") in ("malicious", "suspicious"):
                print(f"    {eng:24} {res.get('category'):11} {res.get('result')}")
    print(f"\n  https://www.virustotal.com/gui/file/{sha}")
    if badge_path:
        write_badge(badge_path, stats, sha, version)
    if mal == 0 and sus == 0:
        print("\n  CLEAN - safe to link on the mod page.")
        return 0
    print("\n  NOT clean. Do NOT link this on the mod page: a public record saying")
    print("  some vendors flagged the file is worse than no link, because it hands")
    print("  the accusation a citation. Investigate the detections first - heuristic")
    print("  false positives on unsigned DLLs that hook a process are common, and")
    print("  most engines will accept a false-positive report.")
    return 2


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("target")
    ap.add_argument("--upload", action="store_true",
                    help="submit the file if VirusTotal has never seen it (PERMANENT, PUBLIC)")
    ap.add_argument("--hash", action="store_true", help="target is already a sha256")
    ap.add_argument("--wait", type=int, default=300, help="seconds to wait for a fresh analysis")
    ap.add_argument("--version", default=None,
                    help="version label shown in the badge, e.g. 0.1.1")
    ap.add_argument("--badge", metavar="PATH", default=None,
                    help="write a shields.io endpoint JSON with the verdict")
    a = ap.parse_args()
    key = api_key()

    if a.hash:
        sha, path = a.target.lower(), None
    else:
        path = pathlib.Path(a.target)
        if not path.exists():
            sys.exit(f"no such file: {path}")
        data = path.read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        print(f"{path.name}  {len(data):,} bytes")
    print(f"sha256 {sha}\n")

    found = get(f"/files/{sha}", key)
    if found:
        print("VirusTotal already has this file:")
        return report(found, sha, a.badge, a.version)

    print("VirusTotal has never seen this file - nothing public exists for it yet.")
    if not a.upload:
        print("\nLookup only. Pass --upload to submit it.")
        print("  NOTE: submitting is PERMANENT and PUBLIC. The result cannot be")
        print("  deleted or retracted, so it is a one-way door and stays opt-in.")
        return 1
    if path is None:
        sys.exit("--upload needs a file, not a bare hash")

    print("\nUploading (permanent, public)...")
    boundary = "----vt" + hashlib.md5(sha.encode()).hexdigest()
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; "
            f"filename=\"{path.name}\"\r\nContent-Type: application/octet-stream\r\n\r\n"
            ).encode() + path.read_bytes() + f"\r\n--{boundary}--\r\n".encode()
    req = urllib.request.Request(API + "/files", data=body, headers={
        "x-apikey": key, "Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(req, timeout=300) as r:
        aid = json.load(r)["data"]["id"]
    print(f"  submitted, analysis {aid[:24]}...")

    deadline = time.time() + a.wait
    while time.time() < deadline:
        time.sleep(15)
        an = get(f"/analyses/{aid}", key)
        status = (an or {}).get("data", {}).get("attributes", {}).get("status")
        print(f"  status: {status}")
        if status == "completed":
            final = get(f"/files/{sha}", key)
            if final:
                print()
                return report(final, sha, a.badge, a.version)
    print("  timed out waiting; check the link above in a few minutes")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
