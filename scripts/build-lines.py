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
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from linelint import lint_text

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
   "initiator": ["Get your mouth on me before somebody comes around that corner.",
                 "Bend over that crate and get your pants out of the way.",
                 {"m": "I have been hard since you walked past me. Do something about it.",
                  "f": "I have been wet since you walked past me. Do something about it."},
                 "Put your hand down my pants and be quick about it.",
                 "I want to fuck you against this wall in the next minute.",
                 "Spread your legs and stop worrying about who is watching."],
   "responder": ["Then stop talking and put your hands on me.",
                 "Fuck me quickly and I will not say a word about it.",
                 "Get your fingers in me before I change my mind.",
                 "Use your mouth on me now, because we do not have long.",
                 "Bend me over it, then, and do not be gentle about it.",
                 "I want to taste you before somebody interrupts us."]},
  "athome": {
   "initiator": ["The door is shut and I am going to fuck you all night.",
                 "Get on the bed and open your legs for me.",
                 {"m": "I want your mouth on my cock and your hands in my hair.",
                  "f": "I want your mouth on my cunt and your hands in my hair."},
                 "Take everything off, because I want to see all of you.",
                 "I am going to have you on every surface in this room.",
                 "Get your ass over here, because I am not asking twice."],
   "responder": ["Then fuck me properly, because nobody is listening tonight.",
                 "My legs are open, so do something about it.",
                 "I want your tongue on me until I cannot think straight.",
                 "Everything comes off, and that includes yours.",
                 "Every surface, you said. I am holding you to that.",
                 "Get over here and put your hands on my ass."]},
  "tender": {
   "initiator": ["Out here under the sky, with your clothes on the ground.",
                 "I want to take my time and taste every part of you.",
                 "Let me get these off you slowly, out where there is room.",
                 "Nobody for a mile, so you can be as loud as you want.",
                 {"m": "I have been hard thinking about this since the sun was up.",
                  "f": "I have been aching for this since the sun was up."},
                 "Lie back in the grass and let me look at you first."],
   "responder": ["Slow and filthy, then. Take all the time you want with me.",
                 "Then get your mouth on me, and do not rush any of it.",
                 "A mile, you said. Good, because I intend to be loud.",
                 "Take them off me, and do it as slowly as you like.",
                 "Look all you want, and then put your hands on me.",
                 "I want to feel you for hours out here."]}},

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

OBSERVER = {
 "mercantile": {
  "alone": ["Huh. Wonder what that would be worth to somebody.",
            "There is a business in this somewhere, I am certain of it.",
            "Nobody is paying for that view, which seems like a waste.",
            "I would charge for this, if I had the nerve to ask.",
            "That is the most anybody has given away free around here all week.",
            "Everything in this place has a price. Apparently not that.",
            "There ought to be a tax on that, and I would collect it.",
            "Free entertainment. Hard to argue with those margins."],
  "crowd": ["Somebody should be selling tickets to this.",
            "Caps say they do not make it as far as the bed.",
            "If I had a stall right here I would retire by morning.",
            "Admission is free today, apparently. Terrible business.",
            "Taking bets, anyone? I rather like the odds.",
            "This crowd would pay. Somebody is leaving caps on the table.",
            "Best value in the settlement and not one person charging.",
            "I would rent out this exact spot by the hour."]},
 "romantic": {
  "alone": ["Look at that. There is still something good left out here.",
            "Somebody ought to be that glad to see me.",
            "It is very nearly enough to make a person hopeful.",
            "That is the first beautiful thing I have seen all week.",
            "I should look away, and I find I do not want to.",
            "The world ended and people still do that. Good.",
            "That makes me miss somebody I really should not.",
            "There are far worse things to stumble across out here."],
  "crowd": ["Say what you like, that is a lovely thing to see.",
            "Everybody is watching and nobody is laughing. That says something.",
            "Let them have it. There is little enough of that going around.",
            "You can all stare if you want. I happen to think it is sweet.",
            "That is the nicest thing to happen here in months.",
            "Put the word out, we have got romance in the settlement.",
            "I would applaud, but that does seem like rather a lot.",
            "Look at this lot all pretending they are not watching."]},
 "vulgar": {
  "alone": ["Well now. Somebody is getting properly fucked over there.",
            "Do not mind me. I am going to stand right here and watch.",
            "That is a considerably better afternoon than I am having.",
            "Listen to the noise those two are making. Good for them.",
            "I have paid caps for a worse view than this one.",
            "They are not even trying to keep their clothes on.",
            "I would climb into that myself if they asked me nicely.",
            "That is going to be in my head for the rest of the week."],
  "crowd": ["Is anybody else watching this, or is it only me?",
            "Go on then, do not stop on our account!",
            "Somebody is getting fucked senseless and we are all just standing here!",
            "Louder, would you! Some of us are standing at the back!",
            "Get your hand under there, you are very nearly done!",
            "That is the best thing to happen in this settlement all month!",
            "We are all watching, and nobody is pretending otherwise!",
            "Half this crowd is taking notes, and I am one of them!"]},
 "reticent": {
  "alone": ["Oh. I should not be standing here right now.",
            "I will simply pretend that I did not see any of that.",
            "That is absolutely none of my business whatsoever.",
            "I am going to go and look at literally anything else.",
            "Well, that is me leaving. Right now. Immediately.",
            "Nobody needs to know that I walked past just then.",
            "I did not need to see that today, or on any other day.",
            "I am going to go and be somewhere else entirely."],
  "crowd": ["Does nobody else find this all a bit much?",
            "I would really rather we all looked somewhere else.",
            "Everyone is just going to stand here, are they.",
            "This is the most uncomfortable I have been all year.",
            "Somebody say something. Or do not. I have no idea.",
            "I am not staying to watch this with all of you.",
            "We could all agree to walk away. Just a thought.",
            "Could we please not be doing this as a group."]},
}

AUDIENCE = {
  "alone": "This watcher is the ONLY one watching. Furtive, awkward, nobody to perform for.",
  "crowd": "Others are watching too. Emboldened, playing to the room, talking past the pair.",
}

NOTES = {
 "quickie": "More than two watching. One stage, about thirty seconds, no guaranteed ending. "
            "Hurried, aware of the audience, past caring.",
 "athome":  "Their own place, or indoors with at most two watching. Five stages, unhurried, "
            "ends in a climax. Private, and in no hurry at all.",
 "tender":  "Out in the open with at most two watching. Three slow stages opening with "
            "kissing. Nothing rough or dominant - this is the affectionate one.",
}


def variants(text):
    """A line is either one string for everybody, or {"m":..., "f":...}.

    Explicit anatomy forces this. A female voice cannot say "my cock", and the
    PARTNER is unknown at authoring time - so self-reference is gendered by the
    speaking voice type (which we always know) and partner-reference stays
    anatomically neutral. Returns [(suffix, gender, text), ...].
    """
    if isinstance(text, str):
        return [("", None, text)]
    return [("_" + g, g, text[g]) for g in ("m", "f")]


def main() -> int:
    lines, seen, problems = [], set(), []
    for persona, scenarios in BANK.items():
        for scenario, roles in scenarios.items():
            for role, texts in roles.items():
                for n, raw in enumerate(texts, 1):
                    for suffix, gender, text in variants(raw):
                        lid = f"{persona}_{scenario}_{role[:4]}_{n:02d}{suffix}"
                        if text in seen:
                            raise SystemExit(f"duplicate line text: {text!r}")
                        seen.add(text)
                        if len(text) < 20:
                            raise SystemExit(f"too short, will drift: {lid} {text!r}")
                        problems.extend(lint_text(lid, text))
                        rec = {"id": lid, "kind": "pair", "persona": persona,
                               "scenario": scenario, "role": role,
                               "text": text, "chars": len(text)}
                        if gender:
                            rec["gender"] = gender
                        lines.append(rec)

    if problems:
        for pr in problems:
            print("LINT: " + pr)
        raise SystemExit(f"{len(problems)} lines would fail the renderer - fix them here")

    for persona, aud in OBSERVER.items():
        for audience, texts in aud.items():
            for n, raw in enumerate(texts, 1):
                for suffix, gender, text in variants(raw):
                    lid = f"{persona}_observer_{audience}_{n:02d}{suffix}"
                    if text in seen:
                        raise SystemExit(f"duplicate line text: {text!r}")
                    seen.add(text)
                    if len(text) < 20:
                        raise SystemExit(f"too short, will drift: {lid} {text!r}")
                    problems.extend(lint_text(lid, text))
                    rec = {"id": lid, "kind": "observer", "persona": persona,
                           "audience": audience, "text": text, "chars": len(text)}
                    if gender:
                        rec["gender"] = gender
                    lines.append(rec)

    slots = len(PERSONAS) * len(NOTES) * 2 * 6 + len(PERSONAS) * len(AUDIENCE) * 8
    gendered = sum(1 for l in lines if l.get("gender"))
    if len(lines) != slots + gendered // 2:
        raise SystemExit(f"expected {slots + gendered // 2} lines, built {len(lines)}")

    doc = {"_": "Voice lines, built by scripts/build-lines.py - edit the bank there, "
                "never this file. kind=pair are R-9 scene barks spoken by the two "
                "actors at OnSceneStarted, selected by the SPEAKER's persona (R-7/R-8), "
                "the scenario and their role. kind=observer are R-12 reactions from a "
                "bystander who came within range while a scene was already running, "
                "selected by THEIR persona and whether anyone else is watching.",
           "personas": PERSONAS, "scenario_notes": NOTES, "audience_notes": AUDIENCE,
           "lines": lines}
    out = pathlib.Path(__file__).resolve().parent.parent / "voice/lines.json"
    out.write_text(json.dumps(doc, indent=2), encoding="utf-8")

    tot = sum(l["chars"] for l in lines)
    print(f"{len(lines)} lines, {tot:,} characters, all unique")
    gl = [l for l in lines if l.get("gender")]
    print(f"  gendered   {len(gl):3} lines ({len(gl)//2} slots split m/f for explicit "
          f"self-reference)")
    for kind in ("pair", "observer"):
        k = [l for l in lines if l["kind"] == kind]
        print(f"  {kind:9} {len(k):3} lines  {sum(x['chars'] for x in k):,} chars")
    for p in PERSONAS:
        sub = [l for l in lines if l["persona"] == p]
        print(f"    {p:11} {len(sub):3}   avg {sum(l['chars'] for l in sub)//len(sub):3} chars")
    print(f"\nper voice: {tot:,} credits.  32 voices: {tot*32:,}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
