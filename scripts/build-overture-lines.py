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


# ---------------------------------------------------------------- stage two
# The approach landed; now the player pushes. Same registers, higher stakes: a
# landing register gets warmer, a missing one gets colder than it did in stage
# one. The NPC is invested by now, so refusing costs them something too.
ESCALATE = {
 "mercantile": {
  "offer":  ["You keep raising the price and I keep saying yes.",
             "Now we are negotiating properly."],
  "charm":  ["Still words. Lovely ones, but still only words.",
             "You are very good at this. I am still waiting."],
  "blunt":  ["You could simply have made me an offer instead.",
             "Blunt is cheap. Try something expensive."],
  "linger": ["Standing closer is not the same as offering me something.",
             "You are taking up my whole afternoon for free."]},
 "romantic": {
  "offer":  ["More caps. Is that genuinely all you have got?",
             "You keep reaching for your pocket instead of your words."],
  "charm":  ["Keep going. Nobody has said anything like that to me in years.",
             "You are going to talk me into something, you know."],
  "blunt":  ["You were doing so well right up until you said that.",
             "That was a step backwards and I think you know it."],
  "linger": ["You are close enough to say something, so say it.",
             "Silence is not the same thing as romance."]},
 "vulgar": {
  "offer":  ["Caps again. You have no imagination at all.",
             "Put it away and tell me what you want to do to me."],
  "charm":  ["That was very pretty and it did nothing for me.",
             "Less poetry. More detail about what happens next."],
  "blunt":  ["Keep talking like that and we are not leaving this room.",
             "Now you are finally saying the right things to me."],
  "linger": ["Standing there breathing at me is not a plan.",
             "Use your mouth for words or use it for something better."]},
 "reticent": {
  "offer":  ["You do not have to give me things. I would rather you did not.",
             "That makes it feel like a transaction, and it is not."],
  "charm":  ["You are saying rather a lot and I cannot keep up with it.",
             "Please slow down. I do not know what to do with that."],
  "blunt":  ["That is a great deal faster than I am able to move.",
             "I am not ready for you to say things like that to me."],
  "linger": ["You are still here, and I have stopped wanting you to go.",
             "That is the longest anybody has stayed. I did notice."]},
}

ESCALATE_RECOIL = {
 "mercantile": ["Not while I am working. Come and find me later.",
                "You are going to cost me customers talking like that."],
 "romantic":   ["Not with an audience. I want it to actually mean something.",
                "Take me somewhere the whole settlement is not listening."],
 "vulgar":     ["You are killing me. Get me somewhere with a door on it.",
                "I want to say yes and I want a wall between us and them."],
 "reticent":   ["Everyone here can hear you. I am going to leave.",
                "Please. Not where people are looking at us like this."],
}

# -------------------------------------------------------------- stage three
# The proposition. By now the register has done its work, so the answer is
# driven by what was accumulated rather than by which words are used here.
PROPOSE = {
 "accept": {
  "mercantile": ["Deal. And I am not even going to haggle over it.",
                 "You have bought yourself an evening. Come on, then."],
  "romantic":   ["Yes. Ask me again somewhere quieter and I will say it louder.",
                 "Take me somewhere that we will not be interrupted."],
  "vulgar":     ["Finally. Get me somewhere with a door and a bed.",
                 "Yes, and I have been thinking about your mouth all afternoon."],
  "reticent":   ["Yes. I am going to regret saying that out loud, but yes.",
                 "Alright. Yes. Before I talk myself back out of it."]},
 "refuse": {
  "mercantile": ["Not at that price. Come back with something better.",
                 "You have not made it worth my while, and you know it."],
  "romantic":   ["No. You did not earn that, and I think you know why.",
                 "Ask me again when you actually mean what you are saying."],
  "vulgar":     ["No. You bored me, and that is worse than offending me.",
                 "Not a chance. You could not even say it properly."],
  "reticent":   ["No. I am sorry. Please do not ask me again today.",
                 "I cannot do this. Not like this, not right now."]},
 "notyet": {
  "mercantile": ["Close. Keep working on it and come back to me.",
                 "You are nearly there. I can almost be persuaded."],
  "romantic":   ["Not yet. But I very much want you to keep trying.",
                 "Ask me again. Not today, but do ask me again."],
  "vulgar":     ["Keep going like that and you will get there. Not yet.",
                 "You are very close to getting exactly what you want."],
  "reticent":   ["Not today. But I do not want you to stop coming over.",
                 "Give me a little longer than this. Please."]},
}

# ------------------------------------------------------------ frame the loop
FAREWELL = {
 "mercantile": ["Come back when you have got something to trade.",
                "Pleasure doing business, whatever it was we did."],
 "romantic":   ["Do not be a stranger. I actually mean that.",
                "Go on, then. And think about me while you are gone."],
 "vulgar":     ["Go on. I will be thinking about it either way.",
                "Leave, then. You know exactly where I sleep."],
 "reticent":   ["Goodbye. Thank you for... yes. Goodbye then.",
                "I will see you around, I hope. If that is alright."],
}

# The NPC remembers. This is the line that makes the store VISIBLE in play -
# without it the relationship number is decoration (R-8 makes the same point
# about reticent).
RETURNING = {
 "mercantile": ["Back again. Are you actually buying this time?",
                "My favourite customer. What is it going to be today?"],
 "romantic":   ["There you are. I was wondering when you would come back.",
                "I have been thinking about the last time you were here."],
 "vulgar":     ["Look who came back for more of it.",
                "I knew you would be back. You have got that look about you."],
 "reticent":   ["Oh. You came back. I did not really expect that.",
                "You are here again. I am glad. Sorry, that was too much."],
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
            add(id=f"ov_{persona}_greet_{n:02d}", kind="greeting", stage=0,
                persona=persona, text=t)

    for persona, regs in RESPONSE.items():
        for reg, texts in regs.items():
            outcome = "land" if LANDS_ON[persona] == reg else "miss"
            for n, t in enumerate(texts, 1):
                add(id=f"ov_{persona}_{reg}_{outcome}_{n:02d}", kind="response", stage=1,
                    persona=persona, register=reg, outcome=outcome, text=t)

    for persona, texts in RECOIL.items():
        for n, t in enumerate(texts, 1):
            add(id=f"ov_{persona}_recoil_{n:02d}", kind="recoil", stage=1,
                persona=persona, register="blunt", outcome="recoil", text=t)

    for persona, regs in ESCALATE.items():
        for reg, texts in regs.items():
            outcome = "land" if LANDS_ON[persona] == reg else "miss"
            for n, t in enumerate(texts, 1):
                add(id=f"ov2_{persona}_{reg}_{outcome}_{n:02d}", kind="response",
                    stage=2, persona=persona, register=reg, outcome=outcome, text=t)

    for persona, texts in ESCALATE_RECOIL.items():
        for n, t in enumerate(texts, 1):
            add(id=f"ov2_{persona}_recoil_{n:02d}", kind="recoil", stage=2,
                persona=persona, register="blunt", outcome="recoil", text=t)

    for outcome, per in PROPOSE.items():
        for persona, texts in per.items():
            for n, t in enumerate(texts, 1):
                add(id=f"ov3_{persona}_{outcome}_{n:02d}", kind="propose", stage=3,
                    persona=persona, outcome=outcome, text=t)

    for kind, table in (("farewell", FAREWELL), ("returning", RETURNING)):
        for persona, texts in table.items():
            for n, t in enumerate(texts, 1):
                add(id=f"ov_{persona}_{kind}_{n:02d}", kind=kind, stage=0,
                    persona=persona, text=t)

    out = pathlib.Path(__file__).resolve().parent.parent / "voice/overture-lines.json"
    out.write_text(json.dumps(
        {"_": "Overture stage one: the first approach. NPC side only - the player's "
              "half is an open question (N-5) and does not block this. Selected by "
              "the NPC's persona (R-7), the register the player chose, and whether "
              "the room is public (N-3).",
         "registers": REGISTERS, "lands_on": LANDS_ON, "lines": lines},
        indent=2), encoding="utf-8")

    chars = sum(l["chars"] for l in lines)
    print(f"OVERTURE: {len(lines)} lines, {chars:,} characters\n")
    names = {0: "frame", 1: "approach", 2: "escalate", 3: "propose"}
    for s in (0, 1, 2, 3):
        sub = [l for l in lines if l["stage"] == s]
        kinds = sorted({x["kind"] for x in sub})
        print(f"  stage {s} {names[s]:9} {len(sub):3} lines   "
              + ", ".join(kinds))
    print(f"\n  per voice type          {chars:>9,} credits")
    for n in (6, 32):
        print(f"  x{n:<2} voice types          {chars*n:>9,}")
    print("")
    print("COVERAGE - this bank is ALREADY the full three stages")
    for n, label in ((6, "core settler voices"), (12, "plus ghouls and elders"),
                     (32, "every voice type")):
        tot = chars * n
        print(f"    x{n:<2} ({label:<22}) {tot:>9,}  = {tot/610000*100:4.1f}% of a Pro month")
    print("")
    print("  A third variant per slot would add about half again.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
