#!/usr/bin/env python3
"""
Build voice/lines.json from the persona bark bank.

    python scripts/build-lines.py

Four personas (R-8) x three scenarios x initiator/responder x six variants = 144
lines. The SPEAKER's persona picks the line, so one persona per line, never a
pair of them.

Persona is derived from the form id (R-7), so an NPC keeps theirs forever with no
save cost - which means these lines are that NPC's voice, not a random flavour
that changes between sessions.

WRITING CONSTRAINT, learned the hard way: short fragmentary lines get rewritten
by BOTH TTS models. "...Yeah. Yeah, alright." measured 0/10 on each. So
`reticent` hesitates through CONTENT - a whole sentence about not being able to
say it - rather than through trailing dots. Same reason no line here is under
about twenty characters.
"""
import json
import pathlib
import re

PERSONAS = {
    "mercantile": "Thinks in trade, value and accounts. Offering themselves IS how they show "
                  "interest - it is the one thing they will not price.",
    "romantic":   "Fancy words, patience, setting. Wants the moment to be worth remembering "
                  "and will say so out loud.",
    "vulgar":     "Crude, direct and explicit. Says exactly what they want in the plainest "
                  "words available, and is not embarrassed by any of it.",
    "reticent":   "Guarded. Struggles to ask, and the struggle is the character - but always "
                  "in whole sentences, because fragments do not survive the renderer.",
}

BANK = {
 "mercantile": {
  "quickie": {
   "initiator": ["I've got five minutes and you're what I'm spending them on.",
                 "Name your price. Whatever it is, we settle up after.",
                 "Best offer you'll get all week, and it expires in about a minute.",
                 "I'll owe you for this. You know I'm good for it.",
                 "Quick trade, and you won't regret the terms.",
                 "Everything here costs something. This is what I give away free."],
   "responder": ["Then you'd better make it worth the price.",
                 "I'll take that deal. Hurry up and close it.",
                 "You're good for it. Go on, then.",
                 "Terms accepted, so stop talking about them.",
                 "I want that in caps later. Right now just come here.",
                 "Cheap at twice the cost. Move."]},
  "athome": {
   "initiator": ["Door's locked and the whole day is paid for. Come and collect.",
                 "I saved this one back for you. Everything else went on the counter.",
                 "I've been putting something aside all week, and it turns out it's you.",
                 "Whatever this costs me, I decided a while ago it was worth it.",
                 "Nobody's buying and nobody's selling tonight. There's only us.",
                 "Take as long as you like. I'm not charging by the hour."],
   "responder": ["Then I'm going to take my time with you.",
                 "You always did know what a thing was worth.",
                 "Keep that offer open all night and I'll keep saying yes.",
                 "That's the first honest deal I've been offered in months.",
                 "I'd have paid double for this. Don't tell anybody.",
                 "Come here and collect, then."]},
  "tender": {
   "initiator": ["Out here nothing costs anything, and that's why I like it.",
                 "I'd trade the whole stash for another hour of this.",
                 "You're the only thing out here I'd call valuable.",
                 "No caps and no counting tonight. Just stay a while.",
                 "I've got nothing to offer you except this, and I'm offering it.",
                 "Everything I own looks like junk next to you."],
   "responder": ["Then keep it all. This is the part I wanted.",
                 "You've got nothing left to trade and I'm still standing here.",
                 "That's worth more than anything in your pack.",
                 "I'd take this over caps on any day you care to name.",
                 "Don't offer me anything. Just stay.",
                 "Come closer. That's the whole price."]}},

 "romantic": {
  "quickie": {
   "initiator": ["I wanted to do this properly. I'll settle for doing it now.",
                 "This deserves candles and a locked door, and it's getting neither.",
                 "Forgive the hurry. I've been thinking about you since morning.",
                 "Not how I pictured it, but it's still you, so it'll do.",
                 "I'd make a speech about it if there were time, and there isn't.",
                 "Come here before the whole moment gets away from us."],
   "responder": ["Save the speech for later and come here now.",
                 "The moment is enough on its own. Take it.",
                 "You always wanted it perfect. This is better than perfect.",
                 "I don't need the candles. I need you.",
                 "Then be quick about it, and still be sweet.",
                 "Hurry up, and mean every second of it."]},
  "athome": {
   "initiator": ["The door is shut and the whole evening belongs to the two of us.",
                 "I've thought about this every single night this week.",
                 "Let me look at you properly for once, without anybody watching.",
                 "There is nowhere I would rather be than in this room with you.",
                 "Stay tonight, and let the rest of the world get on without us.",
                 "You make this ruin feel like somewhere worth living in."],
   "responder": ["Then don't waste a minute of it on talking.",
                 "I've thought about it too, every night, exactly the same.",
                 "Look all you like. I'm not going anywhere tonight.",
                 "You say the loveliest things once the door is locked.",
                 "Let the world get on without us. Come to bed.",
                 "Come here and say that again, closer this time."]},
  "tender": {
   "initiator": ["Look at that sky, and then look at you standing under it.",
                 "The world ended and it still left us something like this.",
                 "Stay out here. Nothing waiting inside is better than this.",
                 "I want to remember you exactly the way you look right now.",
                 "There's nobody out here but us and the wind moving the grass.",
                 "Come here, and let this one be slow."],
   "responder": ["Slow, then. We've got all the dark there is.",
                 "I would stay out here all night with you and not complain once.",
                 "Remember it, then, and I'll remember it too.",
                 "The wind out here is very good at keeping secrets.",
                 "Nothing inside is better than this. You're right about that.",
                 "Come closer and say the whole thing again."]}},

 "vulgar": {
  "quickie": {
   "initiator": ["Bend over that crate and let's not waste the daylight.",
                 "I've been hard since you walked past me. Do something about it.",
                 "Get your hands on me before somebody needs something.",
                 "No more talking. Just get your clothes out of the way.",
                 "I want you filthy and I want you right now.",
                 "Hands on the wall. We've got about a minute."],
   "responder": ["Then stop describing it and get on with it.",
                 "You talk a lot for somebody with his hands that full.",
                 "Filthy is what I came over here for. Go on.",
                 "About time. Hurry up and ruin me properly.",
                 "Quit narrating the thing and start doing it.",
                 "The wall it is, then. Make it count."]},
  "athome": {
   "initiator": ["Door's shut, and I'm going to take my time wrecking you.",
                 "Get on the bed. I've got plans and none of them are polite.",
                 "I want to hear you all night and there's nobody around to complain.",
                 "Clothes off. We are not leaving this room until morning.",
                 "I've thought filthy things about you all week. Let's work through them.",
                 "Lie down and let me be extremely thorough about this."],
   "responder": ["Thorough sounds about right. Get over here.",
                 "Then be loud about it, because nobody is listening.",
                 "Plans, is it. Go ahead and show me the plans.",
                 "Wreck away. I've been waiting on this all week.",
                 "Off they come, and don't you dare be gentle.",
                 "All night, you said. Go on and prove it."]},
  "tender": {
   "initiator": ["Out here under the sky, with your clothes on the ground.",
                 "I want you slow tonight. Filthy, but slow with it.",
                 "Nobody for a mile in any direction. Scream if you feel like it.",
                 "Let me take my time with you out here where there's room.",
                 "I've wanted you like this since the sun was still up.",
                 "Come here. I intend to enjoy this properly."],
   "responder": ["Slow and filthy. I can absolutely work with that.",
                 "Take all the time you want with me.",
                 "A mile in any direction, you said. That'll do nicely.",
                 "Then enjoy it, because I fully intend to.",
                 "Come here and start, before I start without you.",
                 "Clothes on the ground, then. Go on."]}},

 "reticent": {
  "quickie": {
   "initiator": ["I'm not good at asking for this, so I'm just going to ask.",
                 "I don't usually do this, but I'd like to, with you.",
                 "Somebody could walk past right now and I find I don't care.",
                 "This is the part where I normally talk myself out of it.",
                 "I've wanted to say something for weeks. That was the something.",
                 "Don't make me explain it any further. Just say yes."],
   "responder": ["You don't have to explain a thing. The answer is yes.",
                 "I was starting to hope you'd finally ask me that.",
                 "It took you long enough to get the words out.",
                 "Then stop talking yourself out of it and come here.",
                 "Yes. That's all I've got for you, but it is a yes.",
                 "I don't need the explanation either. Come here."]},
  "athome": {
   "initiator": ["The door is shut, which makes this considerably easier to say.",
                 "I'm much better at this than I am at talking about it.",
                 "I've had this conversation in my head about a hundred times.",
                 "Stay tonight. I'm not going to say it any better than that.",
                 "It's quieter in here, and I can manage it when it's quiet.",
                 "I don't let people in. I'm letting you in."],
   "responder": ["Then let me in, and stop apologizing for it.",
                 "You said that just fine. Come here.",
                 "A hundred times in your head, and I only needed the once.",
                 "I'm staying. You don't have to ask me twice.",
                 "Quiet suits you, and so does this.",
                 "I know exactly what it cost you to say that."]},
  "tender": {
   "initiator": ["It's easier out here. I don't know why, but it is.",
                 "I don't say very much. I'd far rather show you.",
                 "There's nobody out here to hear me get the words wrong.",
                 "I've been working up to this for longer than you know.",
                 "Out here I can almost manage to say what I mean.",
                 "Stay a while and I might get the words out yet."],
   "responder": ["Then take your time with the words. I'll wait.",
                 "You don't have to say it. I worked it out a while ago.",
                 "Show me, then, if that's the easier one.",
                 "Nobody out here but me, and I don't mind if you get it wrong.",
                 "Longer than I know? You can tell me about it later.",
                 "I'll wait. I've been waiting anyway."]}},
}

NOTES = {
 "quickie": "More than two watching. One stage, about thirty seconds, no guaranteed ending. "
            "Hurried, aware of the audience, past caring.",
 "athome":  "Their own place, or indoors with at most two watching. Five stages, unhurried, "
            "ends in a climax. Private, and in no hurry at all.",
 "tender":  "Out in the open with at most two watching. Three slow stages opening with "
            "kissing. Nothing rough or dominant - this is the affectionate one.",
}


# Authoring lint. Catching these here costs nothing; catching them in the
# renderer costs six takes per line and looks like model drift.
#
#   British spelling - these are American wasteland characters, and the
#   transcriber writes American, so every check fails on audio that was right.
#   Measured: "apologising" failed 3/3 while the audio was perfect.
#
#   "X in Y" after a noun - unstressed "in" and "and" are near-identical in
#   connected speech. "the wind in the grass" came back as "the wind and the
#   grass" on 2 of 3 takes.
BRITISH = re.compile(r"\b\w+(?:ising|isation|ised|ises|ourite|ogue)\b", re.I)
BRITISH_OK = {"raised", "praised", "noised"}
AMBIGUOUS = re.compile(r"\b(?:wind|rain|sun|light|sound|air|smoke)\s+in\s+the\b", re.I)


def lint_text(lid: str, text: str) -> list:
    bad = []
    for w in BRITISH.findall(text):
        if w.lower() not in BRITISH_OK:
            bad.append(f"{lid}: British spelling {w!r} - the transcriber writes American")
    if AMBIGUOUS.search(text):
        bad.append(f"{lid}: a noun followed by " + chr(34) + "in the" + chr(34) +
                   " - unstressed in/and are indistinguishable, rewrite it")
    return bad


def main() -> int:
    lines, seen, problems = [], set(), []
    for persona, scenarios in BANK.items():
        for scenario, roles in scenarios.items():
            for role, texts in roles.items():
                for n, text in enumerate(texts, 1):
                    lid = f"{persona}_{scenario}_{role[:4]}_{n:02d}"
                    if text in seen:
                        raise SystemExit(f"duplicate line text: {text!r}")
                    seen.add(text)
                    if len(text) < 20:
                        raise SystemExit(f"too short, will drift: {lid} {text!r}")
                    problems.extend(lint_text(lid, text))
                    lines.append({"id": lid, "persona": persona, "scenario": scenario,
                                  "role": role, "text": text, "chars": len(text)})

    if problems:
        for pr in problems:
            print("LINT: " + pr)
        raise SystemExit(f"{len(problems)} lines would fail the renderer - fix them here")

    want = len(PERSONAS) * len(NOTES) * 2 * 6
    if len(lines) != want:
        raise SystemExit(f"expected {want} lines, built {len(lines)}")

    doc = {"_": "R-9 scene barks, spoken at OnSceneStarted. Selected by the SPEAKER's "
                "persona (R-7/R-8), the scenario, and their role in it. Built by "
                "scripts/build-lines.py - edit the bank there, never this file.",
           "personas": PERSONAS, "scenario_notes": NOTES, "lines": lines}
    out = pathlib.Path(__file__).resolve().parent.parent / "voice/lines.json"
    out.write_text(json.dumps(doc, indent=2), encoding="utf-8")

    tot = sum(l["chars"] for l in lines)
    print(f"{len(lines)} lines, {tot:,} characters, all unique")
    for p in PERSONAS:
        sub = [l for l in lines if l["persona"] == p]
        print(f"  {p:11} {len(sub):3} lines   avg {sum(l['chars'] for l in sub)//len(sub):3} chars")
    print(f"\nper voice: {tot:,} credits.  32 voices: {tot*32:,}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
