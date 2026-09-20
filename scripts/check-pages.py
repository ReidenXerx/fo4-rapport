#!/usr/bin/env python3
"""
Validate the generated voice pages: python scripts/check-pages.py

A page whose inline script throws renders its HEADER AND NOTHING ELSE, which
looks almost fine in a screenshot. barks.html shipped blank because an escaped
apostrophe collapsed on the way into the file and closed a JS string early.

So: extract each page's script, run node --check on it, and confirm the data the
page depends on is actually referenced. Exit non-zero on any failure.
"""
import pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGES = {
    "voice/barks.html":    ["TYPES", "LINES", "play/"],
    "voice/audition.html": ["picks", "audition/"],
}


def main() -> int:
    bad = 0
    for name, must in PAGES.items():
        p = ROOT / name
        if not p.exists():
            print(f"{name:24} MISSING"); bad += 1; continue
        t = p.read_text(encoding="utf-8")
        if "<script>" not in t:
            print(f"{name:24} no <script> block"); bad += 1; continue
        js = t.split("<script>", 1)[1].rsplit("</script>", 1)[0]
        with tempfile.TemporaryDirectory() as d:
            f = pathlib.Path(d) / "page.js"
            f.write_text(js, encoding="utf-8")
            r = subprocess.run(["node", "--check", str(f)], capture_output=True, text=True)
        missing = [m for m in must if m not in t]
        if r.returncode or missing:
            print(f"{name:24} FAIL")
            if r.returncode:
                print("   " + (r.stderr.strip().splitlines() or ["?"])[0][:160])
                for line in r.stderr.splitlines():
                    if "SyntaxError" in line:
                        print("   " + line.strip()[:160])
            if missing:
                print(f"   expected content not found: {', '.join(missing)}")
            bad += 1
        else:
            print(f"{name:24} OK  ({len(js):,} chars of JS, {len(t)//1024} KB)")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
