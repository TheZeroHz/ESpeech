#!/usr/bin/env python3
"""
TinyCFG Grammar Compiler v2 — offline compilation for embedded LL(1) parsing.

Novel features for conference-grade TinyCFG:
  - Dual representation: CFG structure rules + terminal command patterns
  - Domain tagging (smarthome, robot, media, security, climate, timer)
  - Resource metadata for Command Dependency Analysis (CDA)
  - Phonetic keys for PER (Phonetic Error Recovery)
  - FIRST/FOLLOW computation and LL(1) conflict detection

Usage:
    python tools/tinycfg_compiler.py tinycfg/grammars/smart_world.cfg
    python tools/tinycfg_compiler.py tinycfg/grammars/smarthome.cfg -o src/grammars/smarthome_grammar.h
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


DOMAIN_IDS = {
    "smarthome": 1,
    "robot": 2,
    "media": 3,
    "security": 4,
    "climate": 5,
    "timer": 6,
    "general": 0,
}

RESOURCE_IDS = {
    "light": 1,
    "fan": 2,
    "lock": 3,
    "curtain": 4,
    "scene": 5,
    "navigation": 6,
    "player": 7,
    "alarm": 8,
    "camera": 9,
    "hvac": 10,
    "timer": 11,
    "none": 0,
}


@dataclass
class Production:
    lhs: str
    rhs: List[str]
    action: Optional[str] = None
    rule_id: int = 0


@dataclass
class CommandPattern:
    tokens: List[str]
    action: str
    arg: str
    domain: str = "general"
    resource: str = "none"
    pattern_id: int = 0
    slot_types: List[str] = field(default_factory=list)  # "", "location", "name", "device"


@dataclass
class Grammar:
    name: str = "grammar"
    version: int = 1
    terminals: Dict[str, List[str]] = field(default_factory=dict)
    slot_locations: List[str] = field(default_factory=list)
    slot_devices: List[str] = field(default_factory=list)
    productions: List[Production] = field(default_factory=list)
    patterns: List[CommandPattern] = field(default_factory=list)
    non_terminals: Set[str] = field(default_factory=set)


SLOT_TOKEN_MAP = {
    "{location}": "SLOT_LOCATION",
    "{name}": "SLOT_NAME",
    "{device}": "SLOT_DEVICE",
}


def parse_cfg(path: Path) -> Grammar:
    text = path.read_text(encoding="utf-8")
    g = Grammar()
    section = None

    for raw in text.splitlines():
        line = raw.split("#")[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
            continue

        if section == "meta":
            if "=" in line:
                k, v = [p.strip() for p in line.split("=", 1)]
                if k == "name":
                    g.name = v.strip('"').strip("'")
                elif k == "version":
                    g.version = int(v)
        elif section == "slots":
            if "=" in line:
                k, v = [p.strip() for p in line.split("=", 1)]
                items = [s.strip() for s in v.split(",") if s.strip()]
                if k == "location":
                    g.slot_locations = items
                elif k == "device":
                    g.slot_devices = items
        elif section == "terminals":
            if ":" in line:
                canon, syns = line.split(":", 1)
                canon = canon.strip()
                synonyms = [s.strip() for s in syns.split(",") if s.strip()]
                g.terminals[canon] = synonyms
            else:
                g.terminals[line.strip()] = []
        elif section == "rules":
            if "->" not in line:
                continue
            lhs, rest = [p.strip() for p in line.split("->", 1)]
            action = None
            if "=>" in rest:
                rest, action = [p.strip() for p in rest.split("=>", 1)]
            rhs = rest.split() if rest and rest != "|" else []
            if rhs == [""]:
                rhs = []
            g.productions.append(Production(lhs=lhs, rhs=rhs, action=action))
            g.non_terminals.add(lhs)
            for sym in rhs:
                if sym and sym[0].isupper():
                    g.non_terminals.add(sym)
        elif section == "patterns":
            domain = "general"
            resource = "none"
            if "@domain" in line:
                parts = line.split("@domain", 1)
                line = parts[0].strip()
                rest = parts[1]
                dm = re.search(r"(\w+)", rest)
                if dm:
                    domain = dm.group(1)
                if "@resource" in rest:
                    rm = re.search(r"@resource\s+(\w+)", rest)
                    if rm:
                        resource = rm.group(1)
            if "=>" not in line:
                continue
            toks_part, action_part = [p.strip() for p in line.split("=>", 1)]
            raw_tokens = toks_part.split()
            tokens: List[str] = []
            slot_types: List[str] = []
            for rt in raw_tokens:
                if rt in SLOT_TOKEN_MAP:
                    tokens.append(SLOT_TOKEN_MAP[rt])
                    slot_types.append(rt.strip("{}"))  # location, name, device
                else:
                    tokens.append(rt)
                    slot_types.append("")
            action_part = action_part.split("@")[0].strip()
            arg = ""
            action = action_part
            m = re.match(r"([A-Z_]+)\(([^)]*)\)", action_part)
            if m:
                action = m.group(1)
                arg = m.group(2)
            elif "(" not in action_part:
                action = action_part
            g.patterns.append(
                CommandPattern(
                    tokens=tokens,
                    action=action,
                    arg=arg,
                    domain=domain,
                    resource=resource,
                    slot_types=slot_types,
                )
            )

    if not g.slot_locations:
        g.slot_locations = [
            "room", "bedroom", "living", "kitchen", "bathroom",
            "garage", "office", "hallway", "garden", "front", "back",
        ]
    if not g.slot_devices:
        g.slot_devices = ["fan", "light", "lamp", "door", "curtain", "camera", "thermostat"]

    return g


def expand_alternatives(g: Grammar) -> List[Production]:
    expanded: List[Production] = []
    for prod in g.productions:
        if not prod.rhs:
            expanded.append(prod)
            continue
        parts: List[List[str]] = [[]]
        for sym in prod.rhs:
            if sym == "|":
                parts.append([])
            else:
                parts[-1].append(sym)
        for alt in parts:
            expanded.append(Production(lhs=prod.lhs, rhs=alt, action=prod.action))
    return expanded


def compute_first(
    g: Grammar, prods: List[Production]
) -> Tuple[Dict[str, Set[str]], Dict[str, Set[str]]]:
    terminals = set(g.terminals.keys()) | {"$"}
    non_terms = g.non_terminals
    first_t: Dict[str, Set[str]] = {t: {t} for t in terminals}
    first_nt: Dict[str, Set[str]] = {nt: set() for nt in non_terms}
    for nt in non_terms:
        first_nt.setdefault(nt, set())

    changed = True
    while changed:
        changed = False
        for p in prods:
            if not p.rhs:
                if "$" not in first_nt[p.lhs]:
                    first_nt[p.lhs].add("$")
                    changed = True
                continue
            for sym in p.rhs:
                if sym in terminals:
                    if sym not in first_nt[p.lhs]:
                        first_nt[p.lhs].add(sym)
                        changed = True
                    break
                else:
                    if sym not in first_nt:
                        first_nt[sym] = set()
                    before = len(first_nt[p.lhs])
                    first_nt[p.lhs] |= first_nt[sym] - {"$"}
                    if len(first_nt[p.lhs]) > before:
                        changed = True
                    if "$" not in first_nt[sym]:
                        break
            else:
                if "$" not in first_nt[p.lhs]:
                    first_nt[p.lhs].add("$")
                    changed = True
    return first_t, first_nt


def check_ll1(g: Grammar, prods: List[Production]) -> List[str]:
    _, first_nt = compute_first(g, prods)
    warnings: List[str] = []
    by_lhs: Dict[str, List[Production]] = {}
    for p in prods:
        by_lhs.setdefault(p.lhs, []).append(p)

    for lhs, rules in by_lhs.items():
        seen: Set[str] = set()
        for p in rules:
            if not p.rhs:
                first = {"$"}
            else:
                sym = p.rhs[0]
                first = {sym} if sym in g.terminals else first_nt.get(sym, set())
            overlap = seen & first
            if overlap:
                warnings.append(
                    f"LL(1) conflict in {lhs}: productions share FIRST({overlap})"
                )
            seen |= first
    return warnings


def resolve_terminal(word: str, g: Grammar, term_to_id: Dict[str, int]) -> Optional[str]:
    """Return canonical terminal name for a word (canonical or synonym)."""
    if word in term_to_id:
        return word
    for canon, syns in g.terminals.items():
        if word in syns:
            return canon
    return None


def c_ident(name: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_]", "_", name).upper()


def phonetic_key(word: str) -> str:
    """Simplified Soundex-like key for PER (Phonetic Error Recovery)."""
    if not word:
        return "0000"
    word = word.upper()
    first = word[0]
    mapping = {
        "BFPV": "1", "CGJKQSXZ": "2", "DT": "3",
        "L": "4", "MN": "5", "R": "6",
    }
    code = first
    for ch in word[1:]:
        digit = ""
        for chars, d in mapping.items():
            if ch in chars:
                digit = d
                break
        if digit and (len(code) == 0 or code[-1] != digit):
            code += digit
        if len(code) >= 4:
            break
    return (code + "0000")[:4]


def emit_header(g: Grammar, prods: List[Production], out: Path) -> None:
    _, first_nt = compute_first(g, prods)
    for i, p in enumerate(prods):
        p.rule_id = i
    for i, pat in enumerate(g.patterns):
        pat.pattern_id = i

    nt_list = sorted(g.non_terminals)
    term_list = sorted(g.terminals.keys())
    term_to_id = {t: i + 1 for i, t in enumerate(term_list)}

    actions: Dict[str, int] = {}
    action_id = 0
    for p in prods:
        if p.action:
            act = p.action.split("(")[0].strip()
            if act not in actions:
                actions[act] = action_id
                action_id += 1
    for pat in g.patterns:
        if pat.action not in actions:
            actions[pat.action] = action_id
            action_id += 1

    guard = f"TINYCFG_{c_ident(g.name)}_GRAMMAR_H"
    lines: List[str] = [
        "// Auto-generated by tinycfg_compiler.py v2 — do not edit.",
        f"// Grammar: {g.name} v{g.version}",
        f"// Patterns: {len(g.patterns)} | Terminals: {len(term_list)} | Actions: {len(actions)}",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdint.h>",
        "",
        f"#define TCFG_GRAMMAR_NAME \"{g.name}\"",
        f"#define TCFG_GRAMMAR_VERSION {g.version}",
        f"#define TCFG_TERMINAL_COUNT {len(term_list)}",
        f"#define TCFG_NONTERMINAL_COUNT {len(nt_list)}",
        f"#define TCFG_PRODUCTION_COUNT {len(prods)}",
        f"#define TCFG_PATTERN_COUNT {len(g.patterns)}",
        f"#define TCFG_ACTION_COUNT {len(actions)}",
        f"#define TCFG_SYNONYM_COUNT_PLACEHOLDER 0",
        "",
        "// --- Domains (for multi-intent CDA) ---",
        "enum TcfgDomain : uint8_t {",
        "    TCFG_DOMAIN_GENERAL = 0,",
    ]
    for name, did in sorted(DOMAIN_IDS.items(), key=lambda x: x[1]):
        if did > 0:
            lines.append(f"    TCFG_DOMAIN_{c_ident(name)} = {did},")
    lines.append("};")
    lines.append("")

    lines.append("// --- Terminals ---")
    lines.append("enum TcfgTerminal : uint8_t {")
    lines.append("    TCFG_TOK_EOF = 0,")
    for i, t in enumerate(term_list):
        lines.append(f"    TCFG_TOK_{c_ident(t)} = {i + 1},")
    lines.append("    TCFG_TOK_UNKNOWN")
    lines.append("};")
    lines.append("")

    lines.append("// --- Semantic actions ---")
    lines.append("enum TcfgAction : uint8_t {")
    lines.append("    TCFG_ACT_NONE = 0,")
    for act, aid in sorted(actions.items(), key=lambda x: x[1]):
        lines.append(f"    TCFG_ACT_{c_ident(act)} = {aid + 1},")
    lines.append("};")
    lines.append("")

    lines.append("// --- Slot tokens (named device / location patterns) ---")
    lines.append("#define TCFG_SLOT_LOCATION 240")
    lines.append("#define TCFG_SLOT_NAME     241")
    lines.append("#define TCFG_SLOT_DEVICE   242")
    lines.append("")
    loc_ids = []
    for loc in g.slot_locations:
        if loc in term_to_id:
            loc_ids.append(f"TCFG_TOK_{c_ident(loc)}")
    lines.append(f"static const uint8_t TCFG_LOCATION_TOKENS[] = {{ {', '.join(loc_ids)} }};")
    lines.append(f"static const uint8_t TCFG_LOCATION_COUNT = {len(loc_ids)};")
    dev_ids = []
    for dev in g.slot_devices:
        if dev in term_to_id:
            dev_ids.append(f"TCFG_TOK_{c_ident(dev)}")
    lines.append(f"static const uint8_t TCFG_DEVICE_TOKENS[] = {{ {', '.join(dev_ids)} }};")
    lines.append(f"static const uint8_t TCFG_DEVICE_COUNT = {len(dev_ids)};")
    lines.append("")

  # Operator tokens for composition
    op_terms = ["and", "or", "then", "after", "before", "if", "until", "while"]
    lines.append("// --- Composition operators (N-intent) ---")
    lines.append("static const uint8_t TCFG_OP_TOKENS[] = {")
    op_ids = []
    for op in op_terms:
        if op in term_to_id:
            op_ids.append(f"TCFG_TOK_{c_ident(op)}")
    lines.append("    " + ", ".join(op_ids) + "," if op_ids else "    0,")
    lines.append("};")
    lines.append(f"static const uint8_t TCFG_OP_TOKEN_COUNT = {len(op_ids)};")
    lines.append("")

    # Synonyms
    lines.append("struct TcfgSynonym { const char* word; uint8_t terminal; const char* phonetic; };")
    lines.append("static const TcfgSynonym TCFG_SYNONYMS[] = {")
    syn_count = 0
    for t in term_list:
        entries = [(t, term_to_id[t])] + [(s, term_to_id[t]) for s in g.terminals[t]]
        for word, tid in entries:
            lines.append(
                f'    {{ "{word}", TCFG_TOK_{c_ident(t)}, "{phonetic_key(word)}" }},'
            )
            syn_count += 1
    lines.append("};")
    lines.append(f"static const uint8_t TCFG_SYNONYM_COUNT = {syn_count};")
    lines.append("")

    # Command patterns (novel: pattern-augmented LL(1))
    lines.append("struct TcfgPattern {")
    lines.append("    const uint8_t* tokens;")
    lines.append("    uint8_t token_count;")
    lines.append("    uint8_t action;")
    lines.append("    const char* arg_template;")
    lines.append("    uint8_t domain;")
    lines.append("    uint8_t resource;")
    lines.append("    const uint8_t* slot_types;  // 0=lit, 1=loc, 2=name, 3=device")
    lines.append("};")
    lines.append("")
    lines.append("enum TcfgSlotType : uint8_t { TCFG_SLOT_LIT=0, TCFG_SLOT_LOC=1, TCFG_SLOT_NM=2, TCFG_SLOT_DEV=3 };")
    lines.append("")

    slot_type_code = {"": 0, "location": 1, "name": 2, "device": 3}

    # Emit token arrays for each pattern
    for i, pat in enumerate(g.patterns):
        tok_ids = []
        slot_bytes = []
        for j, tok in enumerate(pat.tokens):
            st = pat.slot_types[j] if j < len(pat.slot_types) else ""
            slot_bytes.append(str(slot_type_code.get(st, 0)))
            if tok == "SLOT_LOCATION":
                tok_ids.append("TCFG_SLOT_LOCATION")
            elif tok == "SLOT_NAME":
                tok_ids.append("TCFG_SLOT_NAME")
            elif tok == "SLOT_DEVICE":
                tok_ids.append("TCFG_SLOT_DEVICE")
            else:
                canon = resolve_terminal(tok, g, term_to_id)
                if canon:
                    tok_ids.append(f"TCFG_TOK_{c_ident(canon)}")
                else:
                    tok_ids.append("TCFG_TOK_UNKNOWN")
        lines.append(
            f"static const uint8_t TCFG_PAT_{i}_TOKENS[] = {{ {', '.join(tok_ids)} }};"
        )
        lines.append(
            f"static const uint8_t TCFG_PAT_{i}_SLOTS[] = {{ {', '.join(slot_bytes)} }};"
        )
    lines.append("")

    lines.append("static const TcfgPattern TCFG_PATTERNS[] = {")
    for i, pat in enumerate(g.patterns):
        act_id = f"TCFG_ACT_{c_ident(pat.action)}"
        dom_id = DOMAIN_IDS.get(pat.domain, 0)
        res_id = RESOURCE_IDS.get(pat.resource, 0)
        arg_lit = f'"{pat.arg}"' if pat.arg else "nullptr"
        lines.append(
            f"    {{ TCFG_PAT_{i}_TOKENS, {len(pat.tokens)}, "
            f"{act_id}, {arg_lit}, {dom_id}, {res_id}, TCFG_PAT_{i}_SLOTS }},"
        )
    lines.append("};")
    lines.append("")

    # Action name strings for explainability
    lines.append("static const char* TCFG_ACTION_NAMES[] = {")
    lines.append('    "NONE",')
    for act, aid in sorted(actions.items(), key=lambda x: x[1]):
        lines.append(f'    "{act}",')
    lines.append("};")
    lines.append("")

    lines.append("static const char* TCFG_DOMAIN_NAMES[] = {")
    lines.append('    "general", "smarthome", "robot", "media", "security", "climate", "timer",')
    lines.append("};")
    lines.append("")

    lines.append(f"#endif // {guard}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="TinyCFG grammar compiler v2")
    parser.add_argument("grammar", type=Path, help="Path to .cfg grammar file")
    parser.add_argument(
        "-o", "--output", type=Path, default=None,
        help="Output .h path (default: src/grammars/<name>_grammar.h)",
    )
    args = parser.parse_args()

    g = parse_cfg(args.grammar)
    prods = expand_alternatives(g)
    warnings = check_ll1(g, prods)
    for w in warnings:
        print(f"WARNING: {w}", file=sys.stderr)

    out = args.output or Path("src/grammars") / f"{g.name}_grammar.h"
    emit_header(g, prods, out)
    print(f"Compiled '{g.name}' v{g.version} -> {out}")
    print(f"  Terminals: {len(g.terminals)}")
    print(f"  Patterns:  {len(g.patterns)}")
    print(f"  Actions:   {len(set(p.action for p in g.patterns))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
