# Translations

`transmit_ko.ts` is the catalogue. `lupdate` writes it, and nothing else
should: it decides the quoting, the doctype and how an apostrophe is escaped,
and a file written by anything else differs from the one it would write — which
makes the check that the catalogues match the source fail every time.

## Adding a language

Add the `.ts` file to `TS_FILES` in `src/app/CMakeLists.txt` and create it:

```bash
lupdate -locations none -no-obsolete -recursive src -ts translations/transmit_de.ts
```

Nothing else. The language appears in the setting on its own — the list is read
back out of what was built, so a translation that is in the build cannot be
missing from the menu, and one that is not cannot be offered.

## Filling one in

Linguist is the usual way. For a batch, the tables here are applied by
`scripts/apply-translations.py`, which matches on context and source text and
reports every entry that no longer matches anything — which is how a reworded
sentence is noticed rather than quietly keeping its old translation.

```bash
scripts/apply-translations.py translations/transmit_ko.ts translations/korean-2-settings.py
lupdate -locations none -no-obsolete -recursive src -ts translations/transmit_ko.ts
scripts/check-translations.py
```

The tables are kept because they are how the batches were made and how they are
redone after a reword. They are not the catalogue: the `.ts` is.

## What is checked

- `scripts/check-translatable.py` — every string a person reads goes through
  `qsTr()`. Run by `ctest -L quality`.
- `scripts/check-translations.py` — the catalogues are valid XML and every
  translation carries the same placeholders as its source. `"%1 of %2"`
  translated as `"%2 of %1"` reports the opposite of what happened, and nothing
  else would catch it.
- Continuous integration runs `lupdate` and fails if it changes anything, so a
  catalogue cannot fall behind the source.

## Korean coverage

Complete: the shell and its navigation, the home page, and settings. The two
wizards and the report are next. Anything not translated falls back to English
rather than being guessed at.
