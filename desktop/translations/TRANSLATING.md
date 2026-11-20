# Translating FM Player Analyzer

Thank you for helping! Every language file in this folder is picked up by the
app automatically and shows up in the **Language** menu. Texts that are not
translated yet appear in English, so a partial translation is already useful.

## How it works

- The app's original texts are **English**. `fmplayeranalyzer.ts` is the
  template: the list of every text in the app. (`fmplayeranalyzer_en.ts` only
  holds English plural forms.)
- Each language has one file: `fmplayeranalyzer_<code>.ts`, for example
  `fmplayeranalyzer_fr.ts` or `fmplayeranalyzer_zh_CN.ts`. The code is a
  language code, optionally with a country (`pt_BR`, `zh_TW`).
- `translate.py` (Python 3, nothing to install) turns a language file into a
  simple JSON list with the English text and an empty `translation` field,
  and back. That JSON is the easiest way to work, alone or with an AI tool.

## Ways to help

1. **Report a wrong or clumsy text.** Open a
   [Translation issue](https://github.com/ElectroHugin/FM-Player-Analyzer/issues/new?template=4-translation.yml) with a
   screenshot and your suggestion. No files needed.
2. **Improve an existing language.** Export it with `--all`, fix entries,
   import, and send the file (see below).
3. **Add a new language.** Create the file, translate as much as you like,
   send it.

You can send the result as a pull request, or attach the `.ts` file (zipped)
to a Translation issue.

## Step by step

```bash
cd desktop/translations

# New language (here: French)
python translate.py new fr

# Messages that still need a translation -> JSON
python translate.py export fr fr.json
# ... fill in "translation" in fr.json ...
python translate.py import fr fr.json      # also runs the check

# Review an existing language: export everything
python translate.py export zh_CN zh_CN.json --all

# Check one or all languages for errors
python translate.py check fr
```

Each JSON entry looks like this:

```json
{
  "context": "fm::StartScreen",
  "source": "New save",
  "translation": ""
}
```

Plural texts have `translation_plural` instead: a list with one text per
plural form of your language (the file says how many under `plural_forms`).
Languages without plural forms, such as Chinese or Japanese, have one.

Prefer a graphical tool? The `.ts` files open in
[Qt Linguist](https://doc.qt.io/qt-6/linguist-translators.html).

## Rules that keep the app working

`translate.py check` (and the CI) catches broken placeholders, HTML tags,
file filters and plural forms. The other rules are up to you:

- **Keep placeholders**: `%1`, `%2`, `%n` must stay. You may change their
  order in the sentence.
- **Keep HTML** such as `<b>…</b>`, `<br/>` and `<a href="…">…</a>`. Translate
  only the text between the tags; never change `href`.
- **File dialog filters**: in texts like
  `FM exports (*.html *.htm *.csv);;All files (*)` translate only the words.
  Keep `(*.ext *.ext)` and `;;` exactly, with normal (ASCII) brackets and
  semicolons and spaces between the patterns. Full-width characters `（）；`
  make the file dialogs show no files.
- **Keep `&`** in menu texts like `&Language`: it marks the keyboard shortcut.
  Move it to a suitable letter, or drop it in languages without a Latin
  alphabet.
- **Values the user types or that come from the game** stay as they are, for
  example the club name `Retired`. Add a translation in brackets if it helps.
- **Folder and product names** stay as they are: `Sports Interactive`,
  `Football Manager`, `views`, `tactics`, `backups`, `graphics`.

## Terms

**Game terms are fixed, not free to translate.** Whatever the game itself
shows (attributes, roles, positions, personalities, foot strength, playing
time, media handling, column headers) must be written exactly as Football
Manager writes it in your language, letter for letter. Players compare the
app with the game, so a synonym, even a good one, is a mistake here.

The official terms are in
[`desktop/data/fm27/glossary-fm27.csv`](../data/fm27/glossary-fm27.csv):
one row per term, with the English text and the game's own text in German,
French, Italian, Simplified Chinese and Russian. They were read from the game
(FM27), not translated by hand. Use them as they are. For a language that is
not in the glossary yet, take the terms from the game in that language; if
you are unsure about one, say so in your issue or pull request rather than
guessing.

Everything else (buttons, hints, explanations) is yours to translate freely,
in a short and friendly tone. Keep these as they are:

| Term | Meaning |
| --- | --- |
| DWRS | The app's role rating (0-100). Do not translate. |
| FM, FM24, FM27 | Football Manager versions. Not "FM radio". |
| Best XI, B-team | Best starting eleven and second eleven. Translate as the game does. |
| FrA | Free agent marker in the app. Keep. |

And use one word consistently for each of these:

| English | German | Meaning in the app |
| --- | --- | --- |
| save | Spielstand | A career; both the game's `.fm` file and the app's own database for it. A noun, not the verb "to save". |
| export | Export | A list exported from the game (HTML/CSV). |
| weights | Gewichte | Importance of attributes in a rating, not body weight. |
| crest | Wappen | Club badge/logo. |
| follow live | live verfolgen | The app re-reads the save whenever the game saves. |
| shortlist | Shortlist | The game's shortlist. |
| squad registration | Meldeliste | League squad registration rules. |

## Using an AI agent

AI tools translate the JSON well if you give them the context. Tips:

- Give the agent this file (`TRANSLATING.md`) and the exported JSON, and ask
  it to fill in only `translation` / `translation_plural`.
- Work in batches of a few hundred entries and keep the same chat, so the
  terms stay consistent. Ask it to keep a short glossary of the terms it chose.
- Give it the glossary too (`desktop/data/fm27/glossary-fm27.csv`) and tell
  it that game terms must be copied from there exactly.
- Point it to the term table above: "save", "weights" and "crest" are
  frequent mistakes. `context` (the screen a text belongs to) helps with
  short labels.
- Run `python translate.py import <lang> <file>.json` afterwards: it checks
  the result and lists anything broken.
- If you can, run the app in your language and look at a few screens. Short
  labels on buttons and in tables have little room.

A prompt you can start from:

> You are translating the user interface of FM Player Analyzer, a companion
> app for the game Football Manager, into **&lt;language&gt;**. Attached are
> TRANSLATING.md (rules and terms) and a JSON file. For each entry, translate
> the English `source` text into &lt;language&gt;, using `context` (the screen
> it belongs to) for meaning. Write the result into
> `translation` (or one text per plural form into `translation_plural`). Use
> the official Football Manager terms in &lt;language&gt; exactly as they
> appear in the attached glossary-fm27.csv for roles, positions, attributes
> and every other game term; never translate those freely. Follow the rules in TRANSLATING.md exactly:
> keep placeholders (%1, %n), HTML tags, `&` shortcuts and file filters, and
> keep the listed names untranslated. Keep the tone short and friendly, like
> the English text. Return the complete JSON, changing nothing but the
> translation fields.

Please mention in your issue or pull request if the translation was made with
an AI tool and whether a native speaker has reviewed it. Both are welcome; it
just helps us know what to test.

## For maintainers

After adding or changing `tr()` texts in the code:

```bash
cd desktop
lupdate src -ts translations/fmplayeranalyzer.ts -no-obsolete -source-language en
lupdate src -ts translations/fmplayeranalyzer_en.ts -pluralonly -no-obsolete -source-language en
# fill in the English plural forms in fmplayeranalyzer_en.ts, if a new text uses %n
python translations/translate.py sync      # adds them to every language
python translations/translate.py check
```

New texts then show in English in the other languages until a translator
fills them in (`export` lists exactly those). Write new texts in English, with
the game's own English terms.

### Changelog in the app

After an update the app shows the new entries of `CHANGELOG.md` once, in the
interface language ("What's new"); the update hint shows them for a newer
release too. The translations are `desktop/changelog/CHANGELOG.<lang>.md`
(same `<lang>` codes as the `.ts` files). Only the newest sections need a
translation; a version missing there shows in English.

- Keep the headings exactly as in `CHANGELOG.md` (`## Unreleased`,
  `## 2.3.2 – 2026-11-21`): the app finds a version by its heading.
- When a release renames `## Unreleased` to `## X.Y.Z – date`, rename it in
  every translation in the same commit.
- A new language needs its file added to `src/app/resources/app.qrc`.

