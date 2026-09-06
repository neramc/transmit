#!/usr/bin/env python3
"""Fills a .ts file from a table of translations, by context and source text.

Editing XML by hand is how a translation file ends up with a stray tag nobody
notices until lrelease refuses it. The table is Python, the file is written by
a parser, and a source string that no longer exists is reported rather than
silently ignored - that last part is the point: when somebody rewords a
sentence, its translation stops applying, and the only way to find out is to be
told.

    scripts/apply-translations.py translations/transmit_ko.ts ko1.py
    lupdate -locations none -no-obsolete -recursive src -ts translations/transmit_ko.ts

Run lupdate afterwards, always. It is the tool that decides what a .ts file
looks like - the quoting, the doctype, how an apostrophe is escaped - and a
file written by anything else differs from the one lupdate would write, which
makes the check that the catalogues match the source fail forever.
"""

from __future__ import annotations

import argparse
import runpy
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("catalogue", type=Path, help="the .ts file to fill")
    parser.add_argument("table", type=Path, help="a python file defining KO = {context: {...}}")
    arguments = parser.parse_args()

    table = runpy.run_path(str(arguments.table)).get("KO")
    if not isinstance(table, dict):
        print(f"{arguments.table} does not define KO", file=sys.stderr)
        return 2

    tree = ElementTree.parse(arguments.catalogue)
    root = tree.getroot()

    applied = 0
    unused = {(context, source) for context, entries in table.items() for source in entries}

    for context in root.findall("context"):
        name_element = context.find("name")
        if name_element is None or name_element.text not in table:
            continue
        entries = table[name_element.text]
        for message in context.findall("message"):
            source_element = message.find("source")
            translation_element = message.find("translation")
            if source_element is None or translation_element is None:
                continue
            source = source_element.text or ""
            if source not in entries:
                continue
            translation_element.text = entries[source]
            # A translation with this attribute is one Linguist counts as not
            # done, and lrelease leaves out of the built catalogue.
            translation_element.attrib.pop("type", None)
            unused.discard((name_element.text, source))
            applied += 1

    tree.write(arguments.catalogue, encoding="utf-8", xml_declaration=True)

    print(f"{applied} translations written to {arguments.catalogue}")
    if unused:
        print(f"\n{len(unused)} in the table that no longer match anything in the source:",
              file=sys.stderr)
        for context, source in sorted(unused):
            print(f"  {context}: {source!r}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
