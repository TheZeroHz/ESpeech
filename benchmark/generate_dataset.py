#!/usr/bin/env python3
"""
Generate a 500-case realistic TinyCFG benchmark dataset.

Includes STT noise, fillers, typos, glued words, multi-intent variants,
named devices, and negative controls.

Usage:
    python generate_dataset.py
    python generate_dataset.py -o dataset.csv -n 500
"""

from __future__ import annotations

import argparse
import csv
import random
import re
from pathlib import Path

RNG = random.Random(42)

NAMES = [
    "rakib", "rakibs", "john", "johns", "sarah", "sarahs", "mary", "marias",
    "ahmed", "fatima", "david", "emily", "tom", "lisa", "mike", "anna",
    "james", "priya", "omar", "nina", "alex", "kate", "ben", "zoe",
]

LOCATIONS = [
    "room", "bedroom", "kitchen", "living", "bathroom", "garage", "office",
    "hallway", "garden", "front", "back", "living room",
]

FILLERS_PREFIX = [
    "", "", "please ", "okay ", "hey ", "um ", "uh ", "okay um ",
    "can you ", "could you ", "i want to ", "i need to ",
]

FILLERS_MID = [
    "", "", " please", " okay", " now", " right now",
]

FILLERS_SUFFIX = [
    "", "", " please", " okay", " thanks", " now",
]

NOISE_TOKENS = [
    "120", "09309582903", "42", "999", "$$$", "adjfija", "xxx", "uhh",
    "hmm", "er", "like", "so", "well", "this", "the", "a",
]

TYPO_MAP = {
    "turn": ["tun", "trn", "turnn"],
    "on": ["onn", "no"],
    "off": ["of", "offf"],
    "light": ["lite", "ligt", "lights"],
    "fan": ["fn", "fans"],
    "room": ["rom", "rooom"],
    "kitchen": ["kichen", "kitchn"],
    "bedroom": ["bedrom", "bedrrom"],
    "living": ["livng", "livin"],
    "come": ["com", "coem"],
    "here": ["her", "heer"],
    "go": ["goo", "got"],
    "play": ["pla", "paly"],
    "music": ["musik", "muzic"],
    "lock": ["lok", "lck"],
    "unlock": ["unlck", "unock"],
    "door": ["dor", "dorr"],
    "camera": ["camra", "cameras"],
    "check": ["chek", "chck"],
    "arm": ["am", "armm"],
    "alarm": ["alrm", "alarms"],
    "heat": ["het", "heta"],
    "cool": ["col", "cooll"],
    "dock": ["dok", "doc"],
    "stop": ["stp", "stopp"],
    "patrol": ["patrl", "patol"],
    "follow": ["folow", "fllow"],
    "scene": ["sene", "scne"],
    "movie": ["movi", "movei"],
    "volume": ["volum", "volme"],
    "pause": ["puse", "paus"],
    "next": ["nex", "nxt"],
    "timer": ["timr", "tmer"],
    "curtain": ["curtian", "curtains"],
    "open": ["opn", "opeen"],
    "close": ["clos", "clse"],
    "switch": ["swich", "swtch"],
    "lamp": ["lmp", "lamps"],
}

# (template, expect_action, description)
SINGLE_COMMANDS = [
    ("turn on {loc} light", "LIGHT_ON", "Light on location"),
    ("turn off {loc} light", "LIGHT_OFF", "Light off location"),
    ("turn on {loc} fan", "FAN_ON", "Fan on location"),
    ("turn off {loc} fan", "FAN_OFF", "Fan off location"),
    ("turn on {name} {loc} fan", "FAN_ON", "Named fan"),
    ("turn on {name} {loc} light", "LIGHT_ON", "Named light"),
    ("turn on {name} fan", "FAN_ON", "Named fan only"),
    ("switch on {loc} lamp", "LIGHT_ON", "Switch synonym"),
    ("lock {loc} door", "LOCK", "Lock door"),
    ("unlock {name} door", "UNLOCK", "Unlock named door"),
    ("unlock {name} {loc} door", "UNLOCK", "Unlock named location door"),
    ("open {loc} curtain", "CURTAIN_OPEN", "Open curtain"),
    ("close {loc} curtain", "CURTAIN_CLOSE", "Close curtain"),
    ("scene movie", "SCENE", "Movie scene"),
    ("scene night", "SCENE", "Night scene"),
    ("scene party", "SCENE", "Party scene"),
    ("come here", "COME_HERE", "Robot come"),
    ("go to {loc}", "NAV_GO", "Navigate to location"),
    ("go to {name} office", "NAV_GO", "Navigate to named office"),
    ("follow me", "FOLLOW_ME", "Follow user"),
    ("patrol {loc}", "PATROL", "Patrol zone"),
    ("go home", "NAV_HOME", "Go home"),
    ("dock", "NAV_DOCK", "Dock robot"),
    ("stop", "STOP", "Emergency stop"),
    ("wait", "WAIT", "Wait command"),
    ("play music", "MEDIA_PLAY", "Play music"),
    ("play {name} playlist", "MEDIA_PLAY", "Play playlist"),
    ("pause music", "MEDIA_PAUSE", "Pause music"),
    ("volume up", "VOLUME_UP", "Volume up"),
    ("volume down", "VOLUME_DOWN", "Volume down"),
    ("mute", "MUTE", "Mute audio"),
    ("unmute", "UNMUTE", "Unmute audio"),
    ("next song", "NEXT_TRACK", "Next track"),
    ("previous song", "PREVIOUS_TRACK", "Previous track"),
    ("arm alarm", "ALARM_ARM", "Arm security"),
    ("disarm alarm", "ALARM_DISARM", "Disarm security"),
    ("check {loc} camera", "CAMERA_VIEW", "View camera"),
    ("record {loc} camera", "CAMERA_RECORD", "Record camera"),
    ("heat up", "TEMP_UP", "Increase temp"),
    ("cool down", "TEMP_DOWN", "Decrease temp"),
    ("set thermostat to seventy", "TEMP_SET", "Set thermostat"),
    ("set timer for five minutes", "TIMER_SET", "Set timer"),
    ("cancel timer", "TIMER_CANCEL", "Cancel timer"),
]

AND_PAIRS = [
    ("turn on {loc} light", "come here", "LIGHT_ON", "Light and robot"),
    ("turn off {loc} fan", "lock front door", "FAN_OFF", "Fan and lock"),
    ("play music", "volume up", "MEDIA_PLAY", "Media and volume"),
    ("arm alarm", "check garage camera", "ALARM_ARM", "Arm and camera"),
    ("open bedroom curtain", "close living curtain", "CURTAIN_OPEN", "Dual curtains"),
    ("patrol hallway", "go home", "PATROL", "Patrol and home"),
    ("turn on kitchen light", "turn off room fan", "LIGHT_ON", "Dual smarthome"),
    ("heat up", "cool down", "TEMP_UP", "Climate dual"),
    ("scene movie", "turn on living light", "SCENE", "Scene and light"),
    ("go to kitchen", "follow me", "NAV_GO", "Nav and follow"),
]

OR_PAIRS = [
    ("turn on {loc} light", "turn off {loc} fan", "LIGHT_ON", "Light or fan"),
    ("come here", "go home", "COME_HERE", "Come or home"),
    ("play music", "pause music", "MEDIA_PLAY", "Play or pause"),
    ("arm alarm", "disarm alarm", "ALARM_ARM", "Arm or disarm"),
    ("heat up", "cool down", "TEMP_UP", "Heat or cool"),
]

JUXTA_PAIRS = [
    ("turn on {loc} light", "turn off {loc} fan", "LIGHT_ON", "Back-to-back smarthome"),
    ("come here", "go home", "COME_HERE", "Back-to-back nav"),
    ("play music", "volume up", "MEDIA_PLAY", "Back-to-back media"),
    ("arm alarm", "check garage camera", "ALARM_ARM", "Back-to-back security"),
    ("dock", "stop", "NAV_DOCK", "Back-to-back robot"),
]

THEN_PAIRS = [
    ("turn off {name} bedroom fan", "turn on kitchen light", "FAN_OFF", "Then smarthome"),
    ("pause music", "next song", "MEDIA_PAUSE", "Then media"),
    ("disarm alarm", "check cameras", "ALARM_DISARM", "Then security"),
    ("set timer for five minutes", "turn off room fan", "TIMER_SET", "Then timer"),
    ("go to living room", "follow me", "NAV_GO", "Then nav"),
]

NEGATIVE = [
    ("random gibberish words", "Pure garbage"),
    ("hello world foo bar", "English but no command"),
    ("turn on the weather", "Unknown device"),
    ("make me coffee", "Out of grammar"),
    ("xyz abc def", "Random tokens"),
    ("what is the time", "Question not command"),
    ("tell me a joke", "Conversational"),
    ("open the refrigerator", "Unknown appliance"),
    ("send email to boss", "Office task"),
    ("order pizza online", "Web task"),
    ("play football", "Wrong play context"),
    ("turn on the sun", "Nonsense target"),
    ("lock the window", "Unsupported device"),
    ("go to mars", "Unknown location"),
    ("volume sideways", "Nonsense modifier"),
]


def pick_name() -> str:
    return RNG.choice(NAMES)


def pick_loc() -> str:
    return RNG.choice(LOCATIONS)


def fill_template(tpl: str) -> str:
    return tpl.format(name=pick_name(), loc=pick_loc())


def apply_typos(text: str, rate: float = 0.35) -> str:
    words = text.split()
    out = []
    for w in words:
        base = w.lower()
        if base in TYPO_MAP and RNG.random() < rate:
            out.append(RNG.choice(TYPO_MAP[base]))
        else:
            out.append(w)
    return " ".join(out)


def glue_words(text: str) -> str:
    words = text.split()
    if len(words) < 2:
        return text.replace(" ", "")
    # glue 1-3 adjacent command words
    i = RNG.randint(0, max(0, len(words) - 2))
    n = RNG.randint(2, min(4, len(words) - i))
    glued = "".join(words[i : i + n])
    return " ".join(words[:i] + [glued] + words[i + n :])


def camel_case(text: str) -> str:
    parts = re.sub(r"[^a-zA-Z0-9 ]", "", text).split()
    if not parts:
        return text
    return "".join(p.capitalize() if i > 0 else p.lower() for i, p in enumerate(parts))


def inject_noise(text: str, heavy: bool = False) -> str:
    words = text.split()
    count = RNG.randint(2, 5) if heavy else RNG.randint(1, 3)
    for _ in range(count):
        pos = RNG.randint(0, len(words))
        words.insert(pos, RNG.choice(NOISE_TOKENS))
    return " ".join(words)


def add_fillers(text: str) -> str:
    return (
        RNG.choice(FILLERS_PREFIX)
        + text
        + RNG.choice(FILLERS_MID)
        + RNG.choice(FILLERS_SUFFIX)
    ).strip()


def maybe_upper(text: str) -> str:
    if RNG.random() < 0.2:
        return text.upper()
    if RNG.random() < 0.15:
        return text.title()
    return text


def gen_single_clean(n: int) -> list[dict]:
    rows = []
    for _ in range(n):
        tpl, action, desc = RNG.choice(SINGLE_COMMANDS)
        text = fill_template(tpl)
        rows.append({
            "category": "single_clean",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": desc,
        })
    return rows


def gen_multi_and(n: int) -> list[dict]:
    rows = []
    for _ in range(n):
        a, b, action, desc = RNG.choice(AND_PAIRS)
        text = f"{fill_template(a)} and {fill_template(b)}"
        rows.append({
            "category": "multi_and",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": desc,
        })
    return rows


def gen_multi_or(n: int) -> list[dict]:
    rows = []
    for _ in range(n):
        a, b, action, desc = RNG.choice(OR_PAIRS)
        text = f"{fill_template(a)} or {fill_template(b)}"
        rows.append({
            "category": "multi_or",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": desc,
        })
    return rows


def gen_multi_juxta(n: int) -> list[dict]:
    rows = []
    for _ in range(n):
        a, b, action, desc = RNG.choice(JUXTA_PAIRS)
        text = f"{fill_template(a)} {fill_template(b)}"
        rows.append({
            "category": "multi_juxta",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": desc,
        })
    return rows


def gen_named_slot(n: int) -> list[dict]:
    rows = []
    templates = [t for t in SINGLE_COMMANDS if "{name}" in t[0]]
    for _ in range(n):
        tpl, action, desc = RNG.choice(templates)
        text = fill_template(tpl)
        rows.append({
            "category": "named_slot",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": f"Named: {desc}",
        })
    return rows


def gen_stt_noise(n: int) -> list[dict]:
    rows = []
    bases = SINGLE_COMMANDS + [(a, act, d) for a, _, act, d in AND_PAIRS]
    for _ in range(n):
        tpl, action, desc = RNG.choice(bases)
        text = fill_template(tpl)
        text = add_fillers(text)
        text = inject_noise(text, heavy=RNG.random() < 0.5)
        text = maybe_upper(text)
        rows.append({
            "category": "stt_noise",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": f"Noisy STT: {desc}",
        })
    return rows


def gen_glued_dgs(n: int) -> list[dict]:
    rows = []
    for _ in range(n):
        tpl, action, desc = RNG.choice(SINGLE_COMMANDS)
        text = fill_template(tpl)
        mode = RNG.randint(0, 2)
        if mode == 0:
            text = glue_words(text)
        elif mode == 1:
            text = camel_case(text)
        else:
            text = glue_words(camel_case(text))
        rows.append({
            "category": "glued_dgs",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": f"DGS glued: {desc}",
        })
    return rows


def gen_fuzzy_per(n: int) -> list[dict]:
    rows = []
    for _ in range(n):
        tpl, action, desc = RNG.choice(SINGLE_COMMANDS)
        text = apply_typos(fill_template(tpl), rate=RNG.uniform(0.25, 0.55))
        if RNG.random() < 0.4:
            text = add_fillers(text)
        rows.append({
            "category": "fuzzy_per",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": f"Typo recovery: {desc}",
        })
    return rows


def gen_temporal_then(n: int) -> list[dict]:
    rows = []
    for _ in range(n):
        a, b, action, desc = RNG.choice(THEN_PAIRS)
        text = f"{fill_template(a)} then {fill_template(b)}"
        rows.append({
            "category": "temporal_then",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": desc,
        })
    return rows


def gen_cross_domain(n: int) -> list[dict]:
    rows = []
    combos = AND_PAIRS + THEN_PAIRS
    for _ in range(n):
        a, b, action, desc = RNG.choice(combos)
        op = RNG.choice(["and", "then"])
        text = f"{fill_template(a)} {op} {fill_template(b)}"
        rows.append({
            "category": "cross_domain",
            "input": text,
            "expect_pass": 1,
            "expect_action": action,
            "description": f"Cross-domain {op}: {desc}",
        })
    return rows


def gen_negative(n: int) -> list[dict]:
    rows = []
    for _ in range(n):
        text, desc = RNG.choice(NEGATIVE)
        if RNG.random() < 0.3:
            text = add_fillers(text)
        rows.append({
            "category": "negative",
            "input": text,
            "expect_pass": 0,
            "expect_action": "",
            "description": desc,
        })
    return rows


def generate(total: int = 500) -> list[dict]:
    # Category distribution (sums to total)
    weights = {
        "single_clean": 55,
        "multi_and": 45,
        "multi_or": 25,
        "multi_juxta": 35,
        "named_slot": 55,
        "stt_noise": 80,
        "glued_dgs": 45,
        "fuzzy_per": 45,
        "temporal_then": 35,
        "cross_domain": 35,
        "negative": 45,
    }
    assert sum(weights.values()) == total, f"weights sum {sum(weights.values())} != {total}"

    generators = {
        "single_clean": gen_single_clean,
        "multi_and": gen_multi_and,
        "multi_or": gen_multi_or,
        "multi_juxta": gen_multi_juxta,
        "named_slot": gen_named_slot,
        "stt_noise": gen_stt_noise,
        "glued_dgs": gen_glued_dgs,
        "fuzzy_per": gen_fuzzy_per,
        "temporal_then": gen_temporal_then,
        "cross_domain": gen_cross_domain,
        "negative": gen_negative,
    }

    rows: list[dict] = []
    for cat, count in weights.items():
        rows.extend(generators[cat](count))

    RNG.shuffle(rows)
    return rows


def write_csv(rows: list[dict], path: Path) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["id", "category", "input", "expect_pass", "expect_action", "description"])
        for i, r in enumerate(rows, 1):
            w.writerow([
                i, r["category"], r["input"], r["expect_pass"],
                r["expect_action"], r["description"],
            ])


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("-n", "--count", type=int, default=500)
    ap.add_argument("-o", "--output", default="dataset/dataset.csv")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    global RNG
    RNG = random.Random(args.seed)

    rows = generate(args.count)
    out = Path(args.output)
    write_csv(rows, out)

    from collections import Counter
    cats = Counter(r["category"] for r in rows)
    print(f"Wrote {len(rows)} cases to {out}")
    for cat, n in sorted(cats.items()):
        print(f"  {cat}: {n}")


if __name__ == "__main__":
    main()
