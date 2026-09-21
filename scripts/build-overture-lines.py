#!/usr/bin/env python3
"""
Overture - stage one of a persona-driven exchange, and the count that comes with it.

    python scripts/build-overture-lines.py

N-2 says the first job is not authoring thousands of lines, it is finding out how
many ONE exchange needs. This file is that exchange, written out in full, so the
number is measured rather than guessed.

THE MODEL
---------
The player picks a REGISTER to approach with. The NPC has a PERSONA (R-7, derived
from form id). Whether the register matches the persona decides whether the
advance LANDS or MISSES - and the player does not get told the persona, they have
to read it.

    register   appeals to     it is
    offer      mercantile     caps, a gift, something material
    charm      romantic       fancy words, patience, a compliment
    blunt      vulgar         crude, direct, explicit
    linger     reticent       say little, stay, be present

PLACE OVERRIDES BOTH (N-3). "Vulgar in a market reads as harassment; in their own
home it reads as intimacy." So an intimate register in a public room RECOILS even
on the persona it would otherwise land with - the vulgar NPC likes it and still
does not want it shouted across a settlement. That is the one rule here that
makes place mean something rather than being decoration.

WHAT THIS IS NOT
----------------
Stage one only: the first approach. A full exchange escalates, and every stage
multiplies. The count at the bottom is what the rest of the design gets sized
from.

Player lines are NOT here. FO4 player dialogue is voiced by the protagonist and
we cannot match that actor, so whether the player's side is silent, text-only, or
scavenged is an open question - and it does not block authoring the NPC side.
"""
import json
import pathlib

REGISTERS = {
    "offer":  "caps, a gift, something material",
    "charm":  "fancy words, patience, a compliment",
    "blunt":  "crude, direct, explicit",
    "linger": "say little, stay, simply be present",
}

# Which register each persona actually wants. Everything else misses.
LANDS_ON = {"mercantile": "offer", "romantic": "charm",
            "vulgar": "blunt", "reticent": "linger"}

GREETING = {
 "mercantile": ["Something you need, or are you just browsing?",
                "If you are buying, then I am listening."],
 "romantic":   ["Well now. Look who found their way over here.",
                "I was hoping somebody interesting would walk past."],
 "vulgar":     ["You want something, or do you just like the view?",
                "Come to talk, or come to do something about it?"],
 "reticent":   ["Oh. Hello. Did you need something from me?",
                "I did not see you standing there. Sorry."],
}

# persona -> register -> two responses. Whether it reads as landing or missing is
# carried by the writing, and LANDS_ON says which one it is.
RESPONSE = {
 "mercantile": {
  "offer":  ["Now that is how you open a conversation.",
             "You have my attention, and that does not come cheap."],
  "charm":  ["Pretty words. What are they going to cost me?",
             "That is very nice. Now tell me what you actually want."],
  "blunt":  ["Direct. I respect it. I am still not moved.",
             "You could have led with an offer instead of that."],
  "linger": ["You can stand there all day if you like. It is free.",
             "Are you waiting for me to name you a price?"]},
 "romantic": {
  "offer":  ["You think I can be bought? That is almost insulting.",
             "Keep your caps and say something worth hearing."],
  "charm":  ["Say that again, and slower this time.",
             "Nobody has talked to me like that in years."],
  "blunt":  ["That is certainly one way to ruin a moment.",
             "You could at least have worked up to it."],
  "linger": ["You are very quiet. Say something to me.",
             "I would much rather you used actual words."]},
 "vulgar": {
  "offer":  ["Caps? I was hoping for something more interesting.",
             "Put your money away and try that again."],
  "charm":  ["All that talking and you still have not said it.",
             "Very poetic. Now say what you actually mean."],
  "blunt":  ["Finally. Somebody who says it out loud.",
             "That is exactly the right thing to say to me."],
  "linger": ["Standing there is not going to get it done.",
             "You could be using your mouth for something."]},
 "reticent": {
  "offer":  ["I do not want anything. Thank you, though.",
             "Please do not. I would not know what to say."],
  "charm":  ["That is a great many words and I have none back.",
             "You really should not say things like that to me."],
  "blunt":  ["I am going to pretend that I did not hear that.",
             "That is... no. Not like that, please."],
  "linger": ["You are still here. Most people are not.",
             "I do not mind you standing there. That is new."]},
}

# N-3: an intimate register in a public room recoils regardless of persona.
RECOIL = {
 "mercantile": ["Not in front of the stall. Have some sense.",
                "Say that somewhere nobody is counting my caps."],
 "romantic":   ["Here? With all these people? Absolutely not.",
                "You have picked the worst possible room for that."],
 "vulgar":     ["I like it, but half the settlement is listening.",
                "Find me somewhere with a door and ask me again."],
 "reticent":   ["People can hear you. Please stop talking.",
                "Not here. Not with everybody watching us."],
}


def main() -> int:
    lines, seen = [], set()

    def add(**kw):
        if kw["text"] in seen:
            raise SystemExit(f"duplicate: {kw['text']!r}")
        seen.add(kw["text"])
        if len(kw["text"]) < 20:
            raise SystemExit(f"too short, will drift: {kw['id']}")
        lines.append({**kw, "chars": len(kw["text"])})

    for persona, texts in GREETING.items():
        for n, t in enumerate(texts, 1):
            add(id=f"ov_{persona}_greet_{n:02d}", kind="greeting",
                persona=persona, text=t)

    for persona, regs in RESPONSE.items():
        for reg, texts in regs.items():
            outcome = "land" if LANDS_ON[persona] == reg else "miss"
            for n, t in enumerate(texts, 1):
                add(id=f"ov_{persona}_{reg}_{outcome}_{n:02d}", kind="response",
                    persona=persona, register=reg, outcome=outcome, text=t)

    for persona, texts in RECOIL.items():
        for n, t in enumerate(texts, 1):
            add(id=f"ov_{persona}_recoil_{n:02d}", kind="recoil",
                persona=persona, register="blunt", outcome="recoil", text=t)

    out = pathlib.Path(__file__).resolve().parent.parent / "voice/overture-lines.json"
    out.write_text(json.dumps(
        {"_": "Overture stage one: the first approach. NPC side only - the player's "
              "half is an open question (N-5) and does not block this. Selected by "
              "the NPC's persona (R-7), the register the player chose, and whether "
              "the room is public (N-3).",
         "registers": REGISTERS, "lands_on": LANDS_ON, "lines": lines},
        indent=2), encoding="utf-8")

    chars = sum(l["chars"] for l in lines)
    print(f"STAGE ONE: {len(lines)} lines, {chars:,} characters\n")
    for k in ("greeting", "response", "recoil"):
        sub = [l for l in lines if l["kind"] == k]
        print(f"  {k:9} {len(sub):3} lines")
    print(f"\n  per voice type          {chars:>9,} credits")
    for n in (6, 32):
        print(f"  x{n:<2} voice types          {chars*n:>9,}")
    print("\nEXTRAPOLATION - what a full exchange costs")
    print("  Stage one is the approach. A real exchange escalates; assume 3 stages")
    print("  and 3 variants instead of 2:")
    full = chars * 3 * 1.5
    print(f"    3 stages x 1.5 variants  {full:>9,.0f} characters per voice type")
    for n, label in ((6, "core settlers only"), (32, "every voice type")):
        tot = full * n
        print(f"    x{n:<2} ({label:<18}) {tot:>11,.0f}  = {tot/610000*100:4.0f}% of a Pro month")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
