#!/usr/bin/env python3
"""
Build voice/barks.html - the bark browser.

    python scripts/build-barks-page.py

Two views. "By persona" walks one voice through all four personas. "Same
situation, all four" puts the four personas' takes on ONE situation next to each
other in the same voice, which is the only way to actually hear the mechanic:
four characters meeting the same moment differently.

The page's script is syntax-checked before it is written. A page whose script
throws renders its header and nothing else, which looks almost fine - barks.html
shipped blank exactly once for that reason.
"""
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PERS = ["mercantile", "romantic", "vulgar", "reticent"]

CSS = """
:root{--bg:#14120f;--fg:#e8e2d4;--dim:#8a8274;--acc:#c8a24a;--line:#2a2621;--van:#9ab87a;
--mercantile:#c8a24a;--romantic:#c98bb9;--vulgar:#cc7a5c;--reticent:#7fa8c9}
*{box-sizing:border-box}html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 -apple-system,Segoe UI,sans-serif;
display:flex;flex-direction:column}
header{padding:12px 18px;border-bottom:1px solid var(--line);display:flex;gap:14px;
align-items:center;flex-wrap:wrap}
h1{font-size:15px;letter-spacing:.07em;text-transform:uppercase;color:var(--acc);margin:0}
.sub{color:var(--dim);font-size:12px}
.seg{display:flex;border:1px solid var(--line);border-radius:5px;overflow:hidden}
.seg button{background:none;border:0;color:var(--dim);padding:5px 12px;cursor:pointer;
font:inherit;font-size:12px}
.seg button.on{background:var(--acc);color:#14120f}
#wrap{flex:1;display:flex;min-height:0}
#side{width:228px;border-right:1px solid var(--line);overflow-y:auto;flex-shrink:0}
#side input{width:100%;background:#0e0c0a;border:0;border-bottom:1px solid var(--line);
color:var(--fg);padding:8px 13px;font:inherit;font-size:12.5px}
#side button{display:block;width:100%;text-align:left;background:none;border:0;color:var(--fg);
padding:7px 13px;cursor:pointer;font:inherit;font-size:12.5px;border-bottom:1px solid #1d1a16}
#side button:hover{background:#1b1813}
#side button.on{background:#241f19;color:var(--acc);box-shadow:inset 3px 0 0 var(--acc)}
#side small{display:block;color:var(--dim);font-size:10px}
#main{flex:1;overflow-y:auto;padding:16px 22px}
h2{font-size:16px;margin:0 0 6px;color:var(--acc)}
h3{font-size:10.5px;text-transform:uppercase;letter-spacing:.11em;color:var(--dim);
margin:18px 0 5px;border-bottom:1px solid var(--line);padding-bottom:4px}
.row{display:flex;gap:9px;align-items:baseline;padding:4px 0}
.pb{background:#241f19;color:var(--fg);border:1px solid var(--line);min-width:30px;height:28px;
border-radius:4px;cursor:pointer;flex-shrink:0;font:inherit;font-size:10px;padding:0 7px;
margin-right:4px}
.pb:hover{border-color:var(--acc)}
.pb.playing{background:var(--acc);color:#14120f;border-color:var(--acc)}
.pb.van{border-color:#3a4a2a;color:var(--van)}.pb.van.playing{background:var(--van);color:#14120f}
.pb.mercantile{color:var(--mercantile)}.pb.romantic{color:var(--romantic)}
.pb.vulgar{color:var(--vulgar)}.pb.reticent{color:var(--reticent)}
.tx{flex:1}.rl{color:var(--dim);font-size:10px;width:40px;flex-shrink:0;text-transform:uppercase}
.sit{border-bottom:1px solid #1d1a16;padding:8px 0}
.sitl{color:var(--dim);font-size:10px;text-transform:uppercase;letter-spacing:.09em;
margin-bottom:4px}
.loud{color:var(--van);font-size:9px;letter-spacing:.08em;margin-left:7px}
p.note{color:var(--dim);font-size:12px;margin:0 0 12px;max-width:700px}
kbd{background:#241f19;border:1px solid var(--line);border-radius:3px;padding:1px 5px;font-size:11px}
"""

JS = """
var TYPES = __TYPES__, LINES = __LINES__, PERS = __PERS__;
var a = new Audio(), cur = null, queue = [], qi = 0, mode = "persona", vt = TYPES[0];

function esc(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;"); }

function play(b) {
  if (cur) { cur.classList.remove("playing"); }
  if (cur === b && !a.paused) { a.pause(); cur = null; return; }
  a.src = b.dataset.src; a.play(); b.classList.add("playing"); cur = b;
  qi = queue.indexOf(b);
}
a.onended = function () { if (cur) { cur.classList.remove("playing"); } cur = null; };

function btn(src, label, cls) {
  return "<button class=\\"pb " + (cls || "") + "\\" data-src=\\"" + src + "\\">" + label + "</button>";
}
function src(l) { return "play/" + vt.vt + "/" + l.id + ".wav"; }

function pick(f) { return LINES.filter(f); }

function byPersona() {
  var h = "";
  PERS.forEach(function (p) {
    h += "<h3>" + p + "</h3>";
    ["quickie", "athome", "tender"].forEach(function (s) {
      pick(function (l) { return l.p === p && l.k === "pair" && l.s === s; })
        .forEach(function (l, i) {
          h += "<div class=\\"row\\">" + btn(src(l), "&#9654;", p)
            + "<span class=\\"rl\\">" + (i === 0 ? s : (i === 6 ? "reply" : "")) + "</span>"
            + "<span class=\\"tx\\">" + esc(l.t) + "</span></div>";
        });
    });
    h += "<h3>" + p + " &mdash; watching someone else</h3>";
    ["alone", "crowd"].forEach(function (aud) {
      pick(function (l) { return l.p === p && l.k === "observer" && l.a === aud; })
        .forEach(function (l, i) {
          h += "<div class=\\"row\\">" + btn(src(l), "&#9654;", p)
            + "<span class=\\"rl\\">" + (i === 0 ? aud : "") + "</span>"
            + "<span class=\\"tx\\">" + esc(l.t)
            + (aud === "crowd" && i === 0 ? "<span class=\\"loud\\">PROJECTING VOICE</span>" : "")
            + "</span></div>";
        });
    });
  });
  return h;
}

function bySituation() {
  var h = "<p class=\\"note\\">Every block is ONE situation, with the four personas meeting it "
        + "in the same voice. That contrast is the whole mechanic - the same moment, four "
        + "different people.</p>";
  var groups = [], n;
  ["quickie", "athome", "tender"].forEach(function (s) {
    ["initiator", "responder"].forEach(function (r) {
      for (n = 1; n <= 6; n++) {
        groups.push({ label: s + " / " + r + " " + n, k: "pair", s: s, r: r,
                      n: ("0" + n).slice(-2) });
      }
    });
  });
  ["alone", "crowd"].forEach(function (aud) {
    for (n = 1; n <= 8; n++) {
      groups.push({ label: "watching / " + aud + " " + n, k: "observer", a: aud,
                    n: ("0" + n).slice(-2) });
    }
  });
  groups.forEach(function (g) {
    var set = PERS.map(function (p) {
      return pick(function (l) {
        return l.p === p && l.k === g.k && l.n === g.n
          && (g.k === "pair" ? (l.s === g.s && l.r === g.r) : l.a === g.a);
      })[0];
    }).filter(Boolean);
    if (set.length !== 4) { return; }
    h += "<div class=\\"sit\\"><div class=\\"sitl\\">" + g.label + "</div>";
    set.forEach(function (l) {
      h += "<div class=\\"row\\">" + btn(src(l), l.p.slice(0, 4), l.p)
        + "<span class=\\"tx\\">" + esc(l.t) + "</span></div>";
    });
    h += "</div>";
  });
  return h;
}

function render() {
  var m = document.getElementById("main");
  var h = "<h2>" + vt.vt + "</h2><div class=\\"row\\">";
  vt.van.forEach(function (v, i) { h += btn("vanilla/" + v, "vanilla " + (i + 1), "van"); });
  h += "</div>";
  m.innerHTML = h + (mode === "persona" ? byPersona() : bySituation());
  queue = [].slice.call(m.querySelectorAll(".pb"));
  queue.forEach(function (b) { b.onclick = function () { play(b); }; });
  m.scrollTop = 0;
}

function renderList(f) {
  var el = document.getElementById("list");
  el.innerHTML = "";
  TYPES.filter(function (t) {
    return !f || t.vt.toLowerCase().indexOf(f.toLowerCase()) >= 0;
  }).forEach(function (t) {
    var b = document.createElement("button");
    b.innerHTML = t.vt + "<small>" + t.n.toLocaleString() + " vanilla lines</small>";
    b.onclick = function () {
      document.querySelectorAll("#side button").forEach(function (x) {
        x.classList.remove("on"); });
      b.classList.add("on"); vt = t; render();
    };
    el.appendChild(b);
  });
}

document.querySelectorAll("#mode button").forEach(function (b) {
  b.onclick = function () {
    document.querySelectorAll("#mode button").forEach(function (x) {
      x.classList.remove("on"); });
    b.classList.add("on"); mode = b.dataset.m; render();
  };
});
document.getElementById("q").oninput = function (e) { renderList(e.target.value); };
document.addEventListener("keydown", function (e) {
  if (e.code !== "Space" || e.target.tagName === "INPUT") { return; }
  e.preventDefault();
  if (!queue.length) { return; }
  qi = (qi + 1) % queue.length;
  queue[qi].click();
});

renderList("");
document.querySelector("#side button").classList.add("on");
render();
"""

HTML = """<!doctype html><meta charset="utf-8"><title>Rapport barks</title>
<style>__CSS__</style>
<header><h1>Rapport &mdash; barks</h1>
<div class="seg" id="mode"><button class="on" data-m="persona">By persona</button><button
 data-m="situation">Same situation, all four</button></div>
<span class="sub">__NV__ voices &times; __NL__ lines &middot; <kbd>space</kbd> plays the next</span>
</header>
<div id="wrap"><div id="side"><input id="q" placeholder="filter voices..."><div id="list"></div></div>
<div id="main"></div></div>
<script>__JS__</script>"""


def main() -> int:
    V = json.loads((ROOT / "voice/voices.json").read_text(encoding="utf-8"))
    L = json.loads((ROOT / "voice/lines.json").read_text(encoding="utf-8"))
    types = sorted(V["types"].items(), key=lambda x: -x[1]["vanilla_lines"])

    lines = [{"id": l["id"], "p": l["persona"], "k": l.get("kind", "pair"),
              "s": l.get("scenario", ""), "r": l.get("role", ""),
              "a": l.get("audience", ""), "t": l["text"],
              "n": l["id"].rsplit("_", 1)[1]} for l in L["lines"]]
    tdata = [{"vt": vt, "n": d["vanilla_lines"],
              "van": sorted(p.name for p in (ROOT / "voice/vanilla").glob(f"{vt}-vanilla*.wav"))}
             for vt, d in types]

    js = (JS.replace("__TYPES__", json.dumps(tdata))
            .replace("__LINES__", json.dumps(lines))
            .replace("__PERS__", json.dumps(PERS)))
    doc = (HTML.replace("__CSS__", CSS).replace("__JS__", js)
               .replace("__NV__", str(len(types))).replace("__NL__", str(len(lines))))

    # Check the script BEFORE writing the page (the docstring explains why).
    with tempfile.TemporaryDirectory() as d:
        f = pathlib.Path(d) / "page.js"
        f.write_text(js, encoding="utf-8")
        r = subprocess.run(["node", "--check", str(f)], capture_output=True, text=True)
    if r.returncode:
        print("REFUSING to write - the page script does not parse:")
        for line in r.stderr.splitlines()[:8]:
            print("   " + line)
        return 1

    out = ROOT / "voice/barks.html"
    out.write_text(doc, encoding="utf-8")
    print(f"{out.relative_to(ROOT)}  {out.stat().st_size // 1024} KB  "
          f"{len(types)} voices, {len(lines)} lines  (script parses)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
