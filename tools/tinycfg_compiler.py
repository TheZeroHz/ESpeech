#!/usr/bin/env python3
"""
TinyCFG Grammar Compiler (offline phase).

Reads a .cfg grammar file, validates LL(1) structure, computes FIRST sets,
and emits compact C tables for the ESP32 runtime parser.

Usage:
    python tools/tinycfg_compiler.py tinycfg/grammars/robot.cfg
    python tools/tinycfg_compiler.py tinycfg/grammars/robot.cfg -o src/grammars/robot_grammar.h
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


@dataclass
class Production:
    lhs: str
    rhs: List[str]
    action: Optional[str] = None
    rule_id: int = 0


@dataclass
class Grammar:
    name: str = "grammar"
    version: int = 1
    terminals: Dict[str, List[str]] = field(default_factory=dict)
    productions: List[Production] = field(default_factory=list)
    non_terminals: Set[str] = field(default_factory=set)


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

    return g


def expand_alternatives(g: Grammar) -> List[Production]:
    """Expand '|' alternatives in productions into separate rules."""
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

    changed = True
    while changed:
        changed = False
        for p in prods:
            if not p.rhs:
                if "$" not in first_nt[p.lhs]:
                    first_nt[p.lhs].add("$")
                    changed = True
                continue
            for i, sym in enumerate(p.rhs):
                if sym in terminals:
                    if sym not in first_nt[p.lhs]:
                        first_nt[p.lhs].add(sym)
                        changed = True
                    break
                else:
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
    """Basic LL(1) conflict detection for predictive parsing."""
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


def c_ident(name: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_]", "_", name).upper()


def emit_header(g: Grammar, prods: List[Production], out: Path) -> None:
    _, first_nt = compute_first(g, prods)
    for i, p in enumerate(prods):
        p.rule_id = i

    nt_list = sorted(g.non_terminals)
    term_list = sorted(g.terminals.keys())
    actions: Dict[str, int] = {}
    action_id = 0

    for p in prods:
        if p.action:
            act = p.action.split("(")[0].strip()
            if act not in actions:
                actions[act] = action_id
                action_id += 1

    lines: List[str] = [
        "// Auto-generated by tinycfg_compiler.py — do not edit.",
        f"// Grammar: {g.name} v{g.version}",
        "#ifndef TINYCFG_ROBOT_GRAMMAR_H",
        "#define TINYCFG_ROBOT_GRAMMAR_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define TCFG_GRAMMAR_NAME \"{g.name}\"",
        f"#define TCFG_GRAMMAR_VERSION {g.version}",
        f"#define TCFG_TERMINAL_COUNT {len(term_list)}",
        f"#define TCFG_NONTERMINAL_COUNT {len(nt_list)}",
        f"#define TCFG_PRODUCTION_COUNT {len(prods)}",
        f"#define TCFG_ACTION_COUNT {len(actions)}",
        "",
        "// --- Terminals ---",
        "enum TcfgTerminal : uint8_t {",
        "    TCFG_TOK_EOF = 0,",
    ]
    for i, t in enumerate(term_list):
        lines.append(f"    TCFG_TOK_{c_ident(t)} = {i + 1},")
    lines.append("    TCFG_TOK_UNKNOWN")
    lines.append("};")
    lines.append("")

    lines.append("// --- Non-terminals ---")
    lines.append("enum TcfgNonTerminal : uint8_t {")
    for i, nt in enumerate(nt_list):
        lines.append(f"    TCFG_NT_{c_ident(nt)} = {i},")
    lines.append("};")
    lines.append("")

    lines.append("// --- Semantic actions ---")
    lines.append("enum TcfgAction : uint8_t {")
    lines.append("    TCFG_ACT_NONE = 0,")
    for act, aid in sorted(actions.items(), key=lambda x: x[1]):
        lines.append(f"    TCFG_ACT_{c_ident(act)} = {aid + 1},")
    lines.append("};")
    lines.append("")

    # Synonym table: canonical terminal index -> list of strings
    lines.append("struct TcfgSynonym { const char* word; uint8_t terminal; };")
    lines.append("static const TcfgSynonym TCFG_SYNONYMS[] = {")
    for i, t in enumerate(term_list):
        lines.append(f'    {{ "{t}", TCFG_TOK_{c_ident(t)} }},')
        for syn in g.terminals[t]:
            lines.append(f'    {{ "{syn}", TCFG_TOK_{c_ident(t)} }},')
    lines.append("};")
    lines.append(f"static const uint8_t TCFG_SYNONYM_COUNT = {sum(1 + len(g.terminals[t]) for t in term_list)};")
    lines.append("")

    # Productions as compact arrays
    lines.append("struct TcfgProduction {")
    lines.append("    uint8_t lhs;")
    lines.append("    uint8_t rhs_len;")
    lines.append("    uint8_t rhs[8];")
    lines.append("    uint8_t action;")
    lines.append("    const char* arg_template;")
    lines.append("};")
    lines.append("")
    lines.append("static const TcfgProduction TCFG_PRODUCTIONS[] = {")

    sym_to_id: Dict[str, Tuple[str, int]] = {}
    for i, t in enumerate(term_list):
        sym_to_id[t] = ("T", i + 1)
    for i, nt in enumerate(nt_list):
        sym_to_id[nt] = ("N", i)

    for p in prods:
        lhs_id = f"TCFG_NT_{c_ident(p.lhs)}"
        rhs_ids = []
        for sym in p.rhs:
            kind, sid = sym_to_id[sym]
            rhs_ids.append(str(sid))
        while len(rhs_ids) < 8:
            rhs_ids.append("0")
        act = "TCFG_ACT_NONE"
        arg_tpl = 'nullptr'
        if p.action:
            act_name = p.action.split("(")[0].strip()
            act = f"TCFG_ACT_{c_ident(act_name)}"
            if "$" in p.action:
                arg_tpl = f'"{p.action}"'
            elif "=>" not in p.action and "(" not in p.action:
                m = re.search(r'=>\s*(\S+)', p.action or "")
            # Extract literal arg from => LIGHT_ON($OBJECT) or => room_light
            m = re.search(r'=>\s*([A-Z_]+)\(([^)]*)\)', f"=> {p.action}") if p.action else None
            if not m:
                m2 = re.search(r'=>\s*(\S+)', f"=> {p.action}") if p.action else None
                if m2 and m2.group(1) not in actions:
                    arg_tpl = f'"{m2.group(1)}"'
                elif m2 and m2.group(1) in actions:
                    arg_tpl = 'nullptr'
            if p.action and "(" not in p.action.split("=>")[-1]:
                literal = p.action.split("=>")[-1].strip()
                if literal and literal.replace("_", "").isalnum():
                    arg_tpl = f'"{literal}"'

        lines.append(
            f"    {{ {lhs_id}, {len(p.rhs)}, "
            f"{{ {', '.join(rhs_ids)} }}, {act}, {arg_tpl} }},"
        )
    lines.append("};")
    lines.append("")

    # FIRST sets for non-terminals (for documentation / future table-driven parser)
    lines.append("// FIRST sets (terminals as TCFG_TOK_* values, 0 = epsilon)")
    lines.append("struct TcfgFirstSet { uint8_t nt; uint8_t count; uint8_t tokens[16]; };")
    lines.append("static const TcfgFirstSet TCFG_FIRST_SETS[] = {")
    for nt in nt_list:
        tokens = sorted(first_nt.get(nt, set()) - {"$"}, key=lambda x: term_list.index(x) if x in term_list else 99)
        tok_ids = [f"TCFG_TOK_{c_ident(t)}" for t in tokens if t in term_list]
        if "$" in first_nt.get(nt, set()):
            tok_ids = ["0"] + tok_ids
        while len(tok_ids) < 16:
            tok_ids.append("255")
        lines.append(
            f"    {{ TCFG_NT_{c_ident(nt)}, {len([t for t in first_nt.get(nt, set()) if t != '$'])}, "
            f"{{ {', '.join(tok_ids[:16])} }} }},"
        )
    lines.append("};")
    lines.append("")

    lines.append("#endif // TINYCFG_ROBOT_GRAMMAR_H")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="TinyCFG grammar compiler")
    parser.add_argument("grammar", type=Path, help="Path to .cfg grammar file")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output .h path (default: src/grammars/<name>_grammar.h)",
    )
    args = parser.parse_args()

    g = parse_cfg(args.grammar)
    prods = expand_alternatives(g)
    warnings = check_ll1(g, prods)
    for w in warnings:
        print(f"WARNING: {w}", file=sys.stderr)

    out = args.output
    if out is None:
        out = Path("src/grammars") / f"{g.name}_grammar.h"

    emit_header(g, prods, out)
    print(f"Compiled '{g.name}' -> {out}")
    print(f"  Terminals: {len(g.terminals)}, Productions: {len(prods)}, Actions: {len(set(p.action for p in prods if p.action))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
