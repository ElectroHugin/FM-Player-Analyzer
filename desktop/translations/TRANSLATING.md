# Translating FM Player Analyzer

Thank you for helping! Every language file in this folder is picked up by the
app automatically and shows up in the **Language** menu. Texts that are not
translated yet appear in English, so a partial translation is already useful.

## How it works

- The app's original texts are **German**. **English** is the reference
  translation (`fmplayeranalyzer_en.ts`).
- Each language has one file: `fmplayeranalyzer_<code>.ts`, for example
  `fmplayeranalyzer_fr.ts` or `fmplayeranalyzer_zh_CN.ts`. The code is a
  language code, optionally with a country (`pt_BR`, `zh_TW`).
- `translate.py` (Python 3, nothing to install) turns a language file into a
  simple JSON list with German, English and an empty `translation` field, and
  back. That JSON is the easiest way to work, alone or with an AI tool.

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
  "german": "Neuer Spielstand",
  "english": "New save",
  "translation": ""
}
```

Plural texts have `translation_plural` instead: a list with one text per
plural form of your language (the file says how many under `plural_forms`).
Languages without plural forms, such as Chinese or Japanese, have one.

Prefer a graphical tool? The `.ts` files open in
[Qt Linguist](https://doc.qt.io/qt-6/linguist-translators.html). Load
`fmplayeranalyzer_en.ts` next to yours to see the English text.

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

Use the words of Football Manager in your language. For roles, positions,
attributes and duties, take the names from the game itself so players
recognise them. Keep these as they are:

| Term | Meaning |
| --- | --- |
| DWRS | The app's role rating (0-100). Do not translate. |
| FM, FM24, FM27 | Football Manager versions. Not "FM radio". |
| Best XI, B-team | Best starting eleven and second eleven. Translate as the game does. |
| FrA | Free agent marker in the app. Keep. |

And use one word consistently for each of these:

| English | German original | Meaning in the app |
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
- Tell it to translate from the **German** text and use the **English** one to
  resolve doubts. Translating only from English loses meaning ("save",
  "weights", "crest" are frequent mistakes).
- Run `python translate.py import <lang> <file>.json` afterwards: it checks
  the result and lists anything broken.
- If you can, run the app in your language and look at a few screens. Short
  labels on buttons and in tables have little room.

A prompt you can start from:

> You are translating the user interface of FM Player Analyzer, a companion
> app for the game Football Manager, into **&lt;language&gt;**. Attached are
> TRANSLATING.md (rules and terms) and a JSON file. For each entry, translate
> the `german` text into &lt;language&gt;, using `english` as a reference and
> `context` (the screen it belongs to) for meaning. Write the result into
> `translation` (or one text per plural form into `translation_plural`). Use
> the official Football Manager terms in &lt;language&gt; for roles,
> positions and attributes. Follow the rules in TRANSLATING.md exactly:
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
lupdate src -ts translations/fmplayeranalyzer_en.ts -no-obsolete
# translate the new texts in fmplayeranalyzer_en.ts
python translations/translate.py sync      # adds them to every other language
python translations/translate.py check
```

New texts then show in English in the other languages until a translator
fills them in (`export` lists exactly those).
