#!/usr/bin/env python3
"""
Authoring lint for every voice line bank. Import it; do not re-implement it.

This module exists because the rules lived inside build-lines.py while
build-overture-lines.py had its own loop and imported nothing. "favourite"
then went straight through, and six voices each burned twelve renders on a
line that was never going to pass - the exact failure the lint was written
to prevent, one file over.

A rule enforced in one of two places is not enforced.

Each rule is here because it cost real renders:

  BRITISH      The transcriber writes American, so British spelling fails
               every verbatim check while the audio is perfectly correct.
               Measured: "apologising" 0/3, "favourite" 0/12 across six voices.
               These are American wasteland characters anyway.

  AMBIGUOUS    Unstressed "in" and "and" are indistinguishable in connected
               speech. "the wind in the grass" came back as "the wind and the
               grass" on two takes of three.

  TOO SHORT    Under about twenty characters a line is nearly contentless, and
               both TTS models feel free to reinvent it. "...Yeah. Yeah,
               alright." measured 0/10 on each.
"""
import re

# A PATTERN cannot do this job. "advise", "surprise", "promise", "exercise",
# "revise", "compromise" and "disguise" are all spelled -ise in American English,
# and -ising catches "raising", "rising", "praising", "arising". The first
# version of this rule flagged "You keep raising the price" as British.
#
# So: an explicit list. Shorter than the false positives it avoids, and every
# entry is a word whose American form genuinely differs.
BRITISH = {
    # -our
    "colour", "colours", "coloured", "favour", "favours", "favoured", "favourite",
    "favourites", "honour", "honours", "honoured", "behaviour", "behaviours",
    "neighbour", "neighbours", "rumour", "rumours", "armour", "armoured", "humour",
    "labour", "laboured", "odour", "odours", "savour", "savoured", "harbour",
    "parlour", "vapour", "flavour", "flavours", "flavoured", "splendour",
    # -ise where American is -ize
    "apologise", "apologised", "apologising", "apologises",
    "realise", "realised", "realising", "realises",
    "recognise", "recognised", "recognising", "recognises",
    "organise", "organised", "organising", "criticise", "criticised",
    "memorise", "memorised", "prioritise", "summarise", "sympathise",
    "specialise", "specialised", "emphasise", "emphasised", "apologise",
    # -re, -ce, doubled l, and the rest
    "centre", "centres", "theatre", "metre", "metres", "litre", "litres",
    "defence", "offence", "offences", "licence", "practise", "practised",
    "travelled", "travelling", "cancelled", "labelled", "marvellous",
    "jewellery", "grey", "greyer", "whilst", "amongst", "storey", "storeys",
    "aeroplane", "kerb", "plough", "moustache", "pyjamas", "sceptical",
}

AMBIGUOUS = re.compile(r"\b(?:wind|rain|sun|light|sound|air|smoke)\s+in\s+the\b", re.I)
MIN_CHARS = 20


def lint_text(lid, text):
    """Return a list of problems with one line. Empty means it is fine."""
    bad = []
    if len(text) < MIN_CHARS:
        bad.append(f"{lid}: only {len(text)} characters - too short, both models "
                   f"will reinvent it")
    for w in re.findall(r"[A-Za-z]+", text):
        if w.lower() in BRITISH:
            bad.append(f"{lid}: British spelling {w!r} - the transcriber writes "
                       f"American and every check will fail")
    if AMBIGUOUS.search(text):
        bad.append(f"{lid}: a noun followed by " + chr(34) + "in the" + chr(34) +
                   " - unstressed in/and are indistinguishable, rewrite it")
    return bad


def lint_all(lines):
    """lines: iterable of (id, text). Returns every problem found."""
    out = []
    for lid, text in lines:
        out.extend(lint_text(lid, text))
    return out


if __name__ == "__main__":
    # Verify the probe before trusting it: a lint that never fires and one that
    # always passes look identical from the outside.
    CASES = [
        ("Then let me in, and stop apologising for it.", True),
        ("My favourite customer. What is it going to be today?", True),
        ("There is nobody out here but us and the wind in the grass.", True),
        ("Yeah. Yeah.", True),
        ("My favorite customer. What is it going to be today?", False),
        ("Then let me in, and stop apologizing for it.", False),
        ("There is nobody out here but us and the wind moving the grass.", False),
        ("I would take this over caps on any day you care to name.", False),
        ("She raised the price and I praised her for it.", False),
        ("You keep raising the price and I keep saying yes.", False),
        ("I would advise you to promise me something.", False),
        ("That was a surprise, and I am not going to compromise.", False),
        ("The colour of that armour is grey.", True),
    ]
    wrong = 0
    for text, should_fail in CASES:
        got = bool(lint_text("x", text))
        wrong += got != should_fail
        mark = "ok   " if got == should_fail else "WRONG"
        print(f"  {mark} {'flags' if got else 'clean'}  {text[:58]}")
    print(f"\n{wrong} wrong of {len(CASES)}")
    raise SystemExit(1 if wrong else 0)
