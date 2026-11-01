#!/usr/bin/env python3
"""Builds src/app/resources/definitions_fm27.json from the FM27 source tables.

Sources (desktop/data/fm27/, transcribed from in-game screenshots):
    rollen-fm27.csv              Phase;Position;Rolle;Kuerzel;Key;Preferable;Merkmale;Notiz
    rollen-kuerzel-fm27.csv Phase;Rolle;Kuerzel-Vorschlag;Kuerzel-im-Spiel;Positionen
    formationen-fm27.csv         Phase;Formation;Slots;Anzahl;Notiz

Usage (from desktop/):  python3 scripts/make_definitions_fm27.py

FM27 has no duties: every role belongs to one phase, "ip" (in possession) or
"oop" (out of possession). Role ids are the in-game abbreviation; when the same
abbreviation exists in both phases the id gets "-IP" / "-OOP". "role_phases"
records the phase of every role. Weights stay provisional (FM24 tiers) until the
weights step of the FM27 update.
"""

import csv
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DESKTOP = os.path.dirname(HERE)
DATA = os.path.join(DESKTOP, "data", "fm27")
OUT = os.path.join(DESKTOP, "src", "app", "resources", "definitions_fm27.json")
FM24 = os.path.join(DESKTOP, "src", "app", "resources", "definitions.json")

PHASES = {"mit Ball": "ip", "ohne Ball": "oop"}

# In-game attribute spelling -> analyzer attribute name.
ATTRIBUTE_NAMES = {
    "Command Of Area": "Command of Area",
    "One On Ones": "One vs One",
    "Off The Ball": "Off the Ball",
    "Team Work": "Teamwork",
}

POSITION_ORDER = ["GK", "D (C)", "D (L)", "D (R)", "WB (L)", "WB (R)", "DM",
                  "M (C)", "M (L)", "M (R)", "AM (C)", "AM (L)", "AM (R)", "ST (C)"]


def category(position):
    if position == "GK":
        return "Goalkeepers"
    if position.startswith(("D ", "WB")):
        return "Defense"
    if position.startswith(("DM", "M ")):
        return "Midfield"
    return "Attack"


# Starter tactics whose two line-ups differ (in possession, out of possession).
# Real tactics only (Edmund 31.10.2026: formations are no tactics, they stay
# templates for the tactic editor). Role ids as in role_id(); game
# abbreviations from data/fm27/rollen-kuerzel-fm27.csv (checked in the game).
DATA_REVISION = 4
# Formerly shipped (generated from formations); dropped from users' copies.
RETIRED_TACTICS = ["3-4-3 DM Narrow", "3-4-3 DM Wide", "4-2-2-2 DM Wide",
                   "4-2-3-1 DM Wide / 4-3-3 DM Wide", "4-2-3-1 DM Wide / 4-4-1-1 2 DM",
                   "4-3-2-1 DM Narrow", "4-3-3 DM Wide", "4-4-2 Diamond Narrow", "5-1-2-2 DM WB",
                   "5-2-1-2 DM WB"]
TACTICS = {
    # Edmund's standard tactic (updated 01.11.2026 for 2.0.1). The game file
    # ships as src/app/resources/tactics/*.fmf for import into FM27.
    "4-2-3-1 - Bavarian Blitz": {
        "ip": {"GK": "BGK", "DL": "WB-IP", "DCL": "BCB", "DCR": "BCB", "DR": "WB-IP",
               "DMCL": "BBP", "DMCR": "DM-IP", "AML": "IF", "AMC": "AM-IP", "AMR": "IW",
               "STC": "CFD-IP"},
        "oop": {"GK": "SK", "DL": "FB-OOP", "DCL": "CB-OOP", "DCR": "CB-OOP", "DR": "FB-OOP",
                "DMC": "SDM", "MCL": "PCM", "MCR": "CM-OOP", "AML": "TW", "AMR": "TW",
                "STC": "CFD-OOP"},
    },
}

def slot_position(slot):
    """Tactic slot -> game position (FM24 rules: DML/DMR = WB, DL/DR = D)."""
    if slot == "GK":
        return "GK"
    side = {"L": "L", "R": "R"}.get(slot[-1]) if not slot.endswith(("CL", "CR")) else None
    if slot in ("DML", "DMR"):
        return "WB (%s)" % side
    for prefix, base in (("DM", "DM"), ("AM", "AM"), ("ST", "ST"), ("D", "D"), ("M", "M")):
        if slot.startswith(prefix):
            if base == "DM":
                return "DM"
            if base == "ST":
                return "ST (C)"
            return "%s (%s)" % (base, side or "C")
    raise ValueError(slot)


def slot_stratum(slot):
    if slot.startswith("DM"):
        return "Defensive Midfield"
    if slot.startswith("AM"):
        return "Attacking Midfield"
    if slot.startswith("ST"):
        return "Strikers"
    if slot.startswith("D"):
        return "Defense"
    return "Midfield"


def read(name):
    with open(os.path.join(DATA, name), encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter=";"))


def attr_list(text):
    return [ATTRIBUTE_NAMES.get(a, a) for a in text.split("|") if a]


def main():
    abbreviations = {}
    for row in read("rollen-kuerzel-fm27.csv"):
        abbr = row["Kuerzel-im-Spiel"].strip() or row["Kuerzel-Vorschlag"].strip()
        abbreviations[(PHASES[row["Phase"]], row["Rolle"])] = abbr

    roles = {}  # (phase, name) -> {key, preferable, positions}
    for row in read("rollen-fm27.csv"):
        phase = PHASES[row["Phase"]]
        entry = roles.setdefault((phase, row["Rolle"]), {
            "key": attr_list(row["Key"]), "preferable": attr_list(row["Preferable"]),
            "positions": []})
        if (entry["key"], entry["preferable"]) != (attr_list(row["Key"]), attr_list(row["Preferable"])):
            sys.exit("Rolle %s (%s) hat je Position andere Attribute" % (row["Rolle"], phase))
        entry["positions"].append(row["Position"])

    abbr_phases = {}
    for (phase, name) in roles:
        abbr_phases.setdefault(abbreviations[(phase, name)], set()).add(phase)

    def role_id(phase, name):
        abbr = abbreviations[(phase, name)]
        return abbr + ("-" + phase.upper() if len(abbr_phases[abbr]) > 1 else "")

    player_roles, weights, phases, mapping = {}, {}, {}, {p: [] for p in POSITION_ORDER}
    for (phase, name), entry in roles.items():
        rid = role_id(phase, name)
        if rid in phases:
            sys.exit("Doppelte Rollen-ID " + rid)
        player_roles.setdefault(category(entry["positions"][0]), {})[rid] = name
        weights[rid] = {"key": entry["key"], "preferable": entry["preferable"]}
        phases[rid] = phase
        for position in entry["positions"]:
            mapping[position].append(rid)

    # Formation templates per phase. Starter tactics: two independent line-ups
    # (in / out of possession) with default roles; which slot goes with which
    # is decided by the best-XI optimisation, not by the tactic.
    templates = {"ip": {}, "oop": {}}
    for row in read("formationen-fm27.csv"):
        slots = row["Slots"].split("|")
        if len(slots) != 11 or len(set(slots)) != 11:
            sys.exit("Formation %s hat keine 11 Slots" % row["Formation"])
        templates[PHASES[row["Phase"]]][row["Formation"]] = slots

    tactic_roles, tactic_layouts = {}, {}
    known_roles = set(phases)
    for name, tactic in TACTICS.items():
        for phase in ("ip", "oop"):
            if len(tactic[phase]) != 11:
                sys.exit("Taktik %s (%s) hat keine 11 Plätze" % (name, phase))
            for slot, role in tactic[phase].items():
                if role not in known_roles or phases[role] != phase:
                    sys.exit("Taktik %s: Rolle %s passt nicht zu %s" % (name, role, phase))
        tactic_roles[name] = tactic
        layout = {}
        for s in tactic["oop"]:
            if s != "GK":
                layout.setdefault(slot_stratum(s), []).append(s)
        tactic_layouts[name] = layout

    with open(FM24, encoding="utf-8") as f:
        personalities = json.load(f)["personalities"]

    out = {
        "fm_version": "fm27",
        # Raise on every data change that existing users must receive (the app
        # replaces their copy, keeping their own tactics).
        "data_revision": DATA_REVISION,
        "retired_tactics": RETIRED_TACTICS,
        "player_roles": player_roles,
        "role_phases": phases,
        "role_specific_weights": weights,
        "position_to_role_mapping": mapping,
        "formation_templates": templates,
        "tactic_roles": tactic_roles,
        "tactic_layouts": tactic_layouts,
        "personalities": personalities,
    }
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("%d Rollen (%d mit Ball, %d ohne Ball), %d Vorlagen, %d Starttaktiken -> %s" % (
        len(phases), list(phases.values()).count("ip"), list(phases.values()).count("oop"),
        len(templates["ip"]) + len(templates["oop"]), len(tactic_roles), os.path.relpath(OUT, DESKTOP)))


if __name__ == "__main__":
    main()
