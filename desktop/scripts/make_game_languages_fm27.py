#!/usr/bin/env python3
"""Build the FM27 game-language glossary and the import tables from captured game data.

Sources (captured locally, NOT part of the repository):
    <captured>/text-grabber/terms_<language>_<stamp>.tsv
        The game's whole term list per language, written by the FM Text Grabber
        plugin (id<TAB>text). Ids are the same in every language.
    <captured>/Sprachordnung/<lang>/S3-*.txt, S5-*.txt, S6-*.txt
        Screen texts of the player profile position map (S3) and the role
        lists In / Out of Possession (S5, S6), one file per screen and language.
    <captured>/Sprachordnung/<lang>/squad_<lang>.csv, db_<lang>.csv
        FM View Exporter files of the same views in every game language.

Outputs (in the repository):
    desktop/data/fm27/glossary-fm27.csv
        Official game terms per language (reference for translators).
    desktop/src/core/GameLanguageData.cpp
        Tables the import uses to read exports written in another game language.

Usage:
    python3 desktop/scripts/make_game_languages_fm27.py <captured> [--template <csv>]

The raw term lists stay outside the repository; only the terms the app needs are
written out.
"""

import argparse
import collections
import csv
import glob
import os
import re
import sys

LANGS = ["en", "de", "fr", "it", "zh_CN", "ru"]
TERM_FILES = {"en": "english", "de": "german", "fr": "french", "it": "italian",
              "zh_CN": "chinese", "ru": "russian"}
TARGETS = LANGS[1:]

HERE = os.path.dirname(os.path.abspath(__file__))
DESKTOP = os.path.dirname(HERE)
GLOSSARY_OUT = os.path.join(DESKTOP, "data", "fm27", "glossary-fm27.csv")
CPP_OUT = os.path.join(DESKTOP, "src", "core", "GameLanguageData.cpp")
SCHEMA_CPP = os.path.join(DESKTOP, "src", "core", "ImportSchema.cpp")
ROLE_ABBR_CSV = os.path.join(DESKTOP, "data", "fm27", "role-abbreviations-fm27.csv")

# Export columns whose values are game words the import turns back into English.
VALUE_COLUMNS = {
    "Preferred Foot": "preferred_foot",
    "Left Foot": "foot_strength",
    "Right Foot": "foot_strength",
    "Personality": "personality",
    "Media Handling": "media_handling",
    "Playing Time": "playing_time",
    "Inf": "information",
}


def clean(text):
    """Game texts in Chinese carry zero-width spaces between characters."""
    return text.replace("​", "").strip()


# --------------------------------------------------------------------------
# Reading the captured data

def load_terms(folder):
    terms = {}
    for lang, name in TERM_FILES.items():
        files = sorted(glob.glob(os.path.join(folder, "text-grabber", f"terms_{name}_*.tsv")))
        if not files:
            sys.exit(f"Missing term list for {lang} in {folder}/text-grabber")
        table = {}
        with open(files[-1], encoding="utf-8-sig") as handle:
            for line in handle:
                if line.startswith("#") or line.startswith("id\t"):
                    continue
                parts = line.rstrip("\n").split("\t", 1)
                if len(parts) == 2:
                    table[int(parts[0])] = clean(parts[1])
        terms[lang] = table
    return terms


def read_screen(path):
    elements = []
    with open(path, encoding="utf-8-sig") as handle:
        for line in handle:
            if line.startswith("#") or line.startswith("panel\t"):
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 8:
                continue
            elements.append({"x": int(fields[1]), "y": int(fields[2]), "name": fields[6],
                             "text": clean(fields[7])})
    return elements


def read_export(folder, lang, kind):
    path = os.path.join(folder, "Sprachordnung", lang, f"{kind}_{lang}.csv")
    with open(path, encoding="utf-8-sig", newline="") as handle:
        rows = [[clean(cell) for cell in row] for row in csv.reader(handle)]
    return rows[0], rows[1:]


# --------------------------------------------------------------------------
# Matching

class Vote:
    """Collects candidate translations per language and picks the majority."""

    def __init__(self):
        self.counts = {lang: collections.Counter() for lang in TARGETS}

    def add(self, lang, text, weight=1):
        if text:
            self.counts[lang][text] += weight

    def best(self, lang):
        counter = self.counts[lang]
        if not counter:
            return "", False
        ranked = counter.most_common()
        # Flag only a real contest, not a stray id with an unrelated meaning.
        ambiguous = len(ranked) > 1 and ranked[1][1] * 2 >= ranked[0][1]
        return ranked[0][0], ambiguous


def by_term_id(terms, english):
    """Translations of an English game text via the ids that carry it."""
    vote = Vote()
    ids = [i for i, text in terms["en"].items() if text == english]
    for i in ids:
        for lang in TARGETS:
            vote.add(lang, terms[lang].get(i, ""))
    return vote, len(ids)


def role_lists(folder):
    """(phase, english role) -> Vote, from the S5/S6 role lists, checked by trait tags."""
    result = collections.defaultdict(Vote)
    base = os.path.join(folder, "Sprachordnung")
    for path in sorted(glob.glob(os.path.join(base, "en", "S[56]-*_en.txt"))):
        key = os.path.basename(path)[:-len("_en.txt")]
        phase = "ip" if key.startswith("S5") else "oop"
        english = [e["text"] for e in sorted(read_screen(path), key=lambda e: e["y"])
                   if e["name"] == "TacticalRole"]
        for lang in TARGETS:
            other = os.path.join(base, lang, f"{key}_{lang}.txt")
            if not os.path.exists(other):
                continue
            local = [e["text"] for e in sorted(read_screen(other), key=lambda e: e["y"])
                     if e["name"] == "TacticalRole"]
            if len(local) != len(english):
                print(f"warning: {key} {lang}: {len(local)} roles vs {len(english)} in English")
                continue
            for en_role, local_role in zip(english, local):
                result[(phase, en_role)].add(lang, local_role)
    return result


POSITION_SCREENS = {
    "GK": ("Goalkeeper", None), "DC": ("Defender", "Centre"), "D": ("Defender", "Left"),
    "DR": ("Defender", "Right"), "WB": ("Wing-Back", "Left"), "WBR": ("Wing-Back", "Right"),
    "DM": ("Defensive Midfielder", None), "MC": ("Midfielder", "Centre"),
    "M": ("Midfielder", "Left"), "MR": ("Midfielder", "Right"),
    "AMC": ("Attacking Midfielder", "Centre"), "AM": ("Attacking Midfielder", "Left"),
    "AMR": ("Attacking Midfielder", "Right"), "ST": ("Striker", "Centre"),
}
NAME_SIDE = re.compile(r"^(.*?)\s*[(（](.*)[)）]\s*$")


def position_names(folder):
    """Long position names and side names from the S3 position map."""
    names = collections.defaultdict(Vote)
    sides = collections.defaultdict(Vote)
    base = os.path.join(folder, "Sprachordnung")
    for key, (en_name, en_side) in POSITION_SCREENS.items():
        en_path = os.path.join(base, "en", f"S3-{key}_en.txt")
        if not os.path.exists(en_path):
            continue
        en_text = next((e["text"] for e in read_screen(en_path) if e["name"] == "selected-pos-text"), "")
        expected = en_name if en_side is None else f"{en_name} ({en_side})"
        for lang in TARGETS:
            path = os.path.join(base, lang, f"S3-{key}_{lang}.txt")
            if not os.path.exists(path):
                continue
            text = next((e["text"] for e in read_screen(path) if e["name"] == "selected-pos-text"), "")
            if not text:
                continue
            match = NAME_SIDE.match(text)
            local_name, local_side = (match.group(1), match.group(2)) if match else (text, None)
            # A capture where the English screen shows another position than
            # the step asked for (seen for DC) is still good for the name.
            names[en_name].add(lang, local_name)
            if en_side and local_side and en_text == expected:
                sides[en_side].add(lang, local_side)
    return names, sides


POSITION_TOKEN = re.compile(r"^(?P<bases>[^()]+?)\s*(?:\((?P<sides>[^()]*)\))?$")


def split_positions(text):
    groups = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        match = POSITION_TOKEN.match(part)
        if not match:
            return None
        bases = [b.strip() for b in match.group("bases").split("/")]
        groups.append((bases, (match.group("sides") or "").strip()))
    return groups


def exports(folder):
    """Header, value, position, side and country tables from the aligned exports."""
    headers = collections.defaultdict(Vote)
    values = collections.defaultdict(Vote)       # (category, english) -> Vote
    bases = collections.defaultdict(Vote)        # english base code -> Vote
    side_letters = collections.defaultdict(Vote)  # english side letter -> Vote
    countries = collections.defaultdict(Vote)    # english nation text -> Vote
    abbreviations = collections.defaultdict(Vote)  # english best-role abbreviation -> Vote
    for kind in ("squad", "db"):
        en_head, en_rows = read_export(folder, "en", kind)
        uid = en_head.index("Unique ID")
        local = {lang: read_export(folder, lang, kind) for lang in TARGETS}
        for lang, (head, _) in local.items():
            if len(head) != len(en_head):
                sys.exit(f"{kind}_{lang}.csv has {len(head)} columns, English {len(en_head)}")
            for en_name, local_name in zip(en_head, head):
                headers[en_name].add(lang, local_name)
        by_uid = {lang: {row[uid]: row for row in rows} for lang, (_, rows) in local.items()}
        for row in en_rows:
            for lang in TARGETS:
                other = by_uid[lang].get(row[uid])
                if other is None:
                    continue
                for column, (en_value, local_value) in enumerate(zip(row, other)):
                    name = en_head[column]
                    if name in VALUE_COLUMNS and en_value not in ("", "-"):
                        category = VALUE_COLUMNS[name]
                        en_parts = [p.strip() for p in en_value.split(",")]
                        local_parts = [p.strip() for p in local_value.split(",")]
                        if category == "media_handling" and len(en_parts) == len(local_parts):
                            for a, b in zip(en_parts, local_parts):
                                values[(category, a)].add(lang, b)
                        elif category != "media_handling":
                            values[(category, en_value)].add(lang, local_value)
                    elif name in ("Position", "Best Pos"):
                        en_groups, local_groups = split_positions(en_value), split_positions(local_value)
                        if not en_groups or not local_groups or len(en_groups) != len(local_groups):
                            continue
                        for (en_bases, en_sides), (lo_bases, lo_sides) in zip(en_groups, local_groups):
                            if len(en_bases) == len(lo_bases):
                                for a, b in zip(en_bases, lo_bases):
                                    bases[a].add(lang, b)
                            if len(en_sides) == len(lo_sides):
                                for a, b in zip(en_sides, lo_sides):
                                    side_letters[a].add(lang, b)
                    elif name in ("Nation", "2nd Nat") and en_value and local_value != en_value:
                        countries[en_value].add(lang, local_value)
                    elif name == "Best Role" and en_value not in ("", "-"):
                        abbreviations[en_value].add(lang, local_value)
    return headers, values, bases, side_letters, countries, abbreviations


def nation_names():
    """English country names the import already knows (ImportSchema.cpp, nationCode())."""
    with open(SCHEMA_CPP, encoding="utf-8") as handle:
        text = handle.read()
    body = text[text.index("QString nationCode("):]
    return sorted(set(re.findall(r'QStringLiteral\("([^"]+)"\), QStringLiteral\("[A-Z]{3}"\)', body)))


# --------------------------------------------------------------------------
# Output

def cpp_string(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    if all(ord(c) < 128 for c in text):
        return f'QStringLiteral("{escaped}")'
    return f'QString::fromUtf8("{escaped}")'


def write_cpp(tables):
    lines = [
        "// Generated by desktop/scripts/make_game_languages_fm27.py - do not edit by hand.",
        "// Official FM27 game texts per language, read from the game itself (term list,",
        "// screens and FM View Exporter files in every game language).",
        "",
        '#include "GameLanguageData.h"',
        "",
        "namespace fm {",
        "",
        "const QHash<QString, GameLanguageTable> &fm27GameLanguageTables()",
        "{",
        "    static const QHash<QString, GameLanguageTable> tables = [] {",
        "        QHash<QString, GameLanguageTable> all;",
    ]
    for lang in TARGETS:
        lines.append(f"        {{")
        lines.append(f"            GameLanguageTable &t = all[QStringLiteral(\"{lang}\")];")
        for member, mapping in tables[lang].items():
            for local, english in sorted(mapping.items()):
                lines.append(f"            t.{member}.insert({cpp_string(local)}, {cpp_string(english)});")
        lines.append("        }")
    lines += [
        "        return all;",
        "    }();",
        "    return tables;",
        "}",
        "",
        "} // namespace fm",
        "",
    ]
    with open(CPP_OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captured", help="folder with text-grabber/ and Sprachordnung/")
    parser.add_argument("--template", help="glossary template (id;category;fill_from;en;...)")
    args = parser.parse_args()

    terms = load_terms(args.captured)
    roles = role_lists(args.captured)
    pos_names, side_names = position_names(args.captured)
    headers, values, bases, side_letters, countries, abbreviations = exports(args.captured)

    glossary = []  # dicts: id, category, en, <langs>, source, note

    def add(row_id, category, english, vote, source, note=""):
        row = {"id": row_id, "category": category, "en": english, "source": source}
        flags = []
        for lang in TARGETS:
            text, ambiguous = vote.best(lang) if vote else ("", False)
            row[lang] = text
            if ambiguous:
                flags.append(lang)
        if flags:
            note = (note + "; " if note else "") + "several game texts for " + ", ".join(flags)
        missing = [lang for lang in TARGETS if not row[lang]]
        if missing:
            note = (note + "; " if note else "") + "not captured: " + ", ".join(missing)
        row["note"] = note
        glossary.append(row)

    seen = set()

    def terms_row(row_id, category, english, note=""):
        vote, count = by_term_id(terms, english)
        add(row_id, category, english, vote if count else None, "term list" if count else "", note)
        seen.add(row_id)

    template_rows = []
    if args.template:
        with open(args.template, encoding="utf-8-sig") as handle:
            template_rows = list(csv.DictReader(handle, delimiter=";"))

    # Template categories with a better source than the term list. Attribute
    # names and column headers come from the export headers (what the game
    # writes into an export); the term list fills the rest.
    for row in template_rows:
        category, english, row_id = row["category"], row["en"], row["id"]
        if category in ("attribute", "column") and english in headers:
            if category == "attribute":
                add(row_id, category, english, headers[english], "export", row.get("note", ""))
            continue
        if category in ("role", "role_abbr", "position_name", "position_code", "side_code",
                        "side_name", "preferred_foot", "personality", "playing_time", "foot_strength"):
            continue
        terms_row(row_id, category, english, row.get("note", ""))

    # Positions
    for en_name in sorted(pos_names):
        add(f"position_name.{en_name}", "position_name", en_name, pos_names[en_name], "screen S3")
    for en_side in ("Left", "Centre", "Right"):
        add(f"side_name.{en_side}", "side_name", en_side, side_names.get(en_side), "screen S3")
    for code in sorted(bases):
        add(f"position_code.{code}", "position_code", code, bases[code], "export")
    for letter in sorted(side_letters):
        add(f"side_code.{letter}", "side_code", letter, side_letters[letter], "export")

    # Roles and their abbreviations
    for (phase, en_role) in sorted(roles):
        add(f"role.{phase}.{en_role}", "role", en_role, roles[(phase, en_role)], "screen S5/S6")
    template_roles = {(r["id"].split(".")[1], r["en"]) for r in template_rows if r["category"] == "role"}
    for phase, en_role in sorted(template_roles - set(roles)):
        add(f"role.{phase}.{en_role}", "role", en_role, None, "", "role list not captured yet")
    with open(ROLE_ABBR_CSV, encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter=";"):
            phase = "ip" if row["Phase"].startswith("in") else "oop"
            abbr = row["Abbreviation-In-Game"]
            add(f"role_abbr.{phase}.{abbr}", "role_abbr", abbr, abbreviations.get(abbr),
                "export" if abbr in abbreviations else "", f"{row['Role']}")

    # Export values
    for (category, english) in sorted(values):
        add(f"{category}.{english}", category, english, values[(category, english)], "export")
    known = {(r["category"], r["en"]) for r in glossary}
    for row in template_rows:
        if row["category"] in ("personality", "playing_time", "foot_strength", "preferred_foot") \
                and (row["category"], row["en"]) not in known:
            terms_row(row["id"], row["category"], row["en"], row.get("note", ""))

    # Export column headers
    attributes = {r["en"] for r in glossary if r["category"] == "attribute"}
    for en_header in sorted(set(headers) - attributes):
        add(f"column.{en_header}", "column", en_header, headers[en_header], "export")

    glossary.sort(key=lambda r: (r["category"], r["id"]))
    with open(GLOSSARY_OUT, "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter=";", lineterminator="\n")
        writer.writerow(["id", "category", "en"] + TARGETS + ["source", "note"])
        for row in glossary:
            writer.writerow([row["id"], row["category"], row["en"]] + [row[l] for l in TARGETS]
                            + [row["source"], row["note"]])

    # Import tables: local text -> English text, per language.
    countries_all = collections.defaultdict(Vote)
    for name in nation_names():
        vote, count = by_term_id(terms, name)
        if count:
            countries_all[name] = vote
    for name, vote in countries.items():
        for lang in TARGETS:
            for text, n in vote.counts[lang].items():
                countries_all[name].add(lang, text, 100 * n)

    tables = {}
    for lang in TARGETS:
        def mapping(source, warn=True):
            out = {}
            for english, vote in source.items():
                text, _ = vote.best(lang)
                if text and text != english:
                    if text in out and out[text] != english:
                        if warn:
                                print(f"warning: {lang} '{text}' means both '{out[text]}' and '{english}'")
                        continue
                    out[text] = english
            return out
        header_map = {}
        for english, vote in headers.items():
            text, _ = vote.best(lang)
            if text:
                header_map[text] = english
        # Values from the glossary: export observations plus term-list texts
        # (personalities the sample exports did not contain). Preferred foot
        # only as the export writes it ("Right-Footed", not "Right").
        value_map = {}
        for row in glossary:
            category = row["category"]
            if category not in set(VALUE_COLUMNS.values()):
                continue
            if category == "preferred_foot" and row["source"] != "export":
                continue
            text = row[lang]
            if text and value_map.setdefault(f"{category}|{text}", row["en"]) != row["en"]:
                print(f"warning: {lang} {category} '{text}' means both "
                      f"'{value_map[f'{category}|{text}']}' and '{row['en']}'")
        tables[lang] = {
            "headers": header_map,
            "values": value_map,
            "positions": mapping(bases),
            "sides": mapping(side_letters),
            # Synonyms ("Turkey"/"Türkiye", name/code) lead to the same nation code.
            "countries": mapping(countries_all, warn=False),
        }
    write_cpp(tables)
    print(f"{len(glossary)} glossary rows -> {GLOSSARY_OUT}")
    print(f"import tables -> {CPP_OUT}")


if __name__ == "__main__":
    main()
