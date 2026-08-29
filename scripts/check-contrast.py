#!/usr/bin/env python3
"""Checks that every pair of colours a reader has to tell apart is legible.

The palette lives in src/ui/qml/theme/Colors.qml and this reads it from there,
so the two cannot drift: a value changed in the interface is checked here on
the next run rather than the next complaint.

The thresholds are WCAG 2.1: 4.5:1 for ordinary text, 3:1 for large text and
for the edges of a control against what is behind it. docs/design.md section 33
requires sufficient contrast and section 9 requires that colour is never the
only carrier of meaning; this covers the first, and the second is a matter for
review rather than a script.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

COLORS_QML = Path(__file__).resolve().parent.parent / "src/ui/qml/theme/Colors.qml"

TEXT = 4.5
LARGE_TEXT = 3.0
UI = 3.0
DISABLED_TEXT = 2.5

# A border has to be visible against what it surrounds; below this it may as
# well not be drawn.
DECORATIVE_BORDER = 1.15

# CIE76. Ten is comfortably past "obviously a different colour" and well short
# of demanding that the palette be garish.
DISTINGUISHABLE = 25.0


def channel(value: int) -> float:
    c = value / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def luminance(hex_color: str) -> float:
    raw = hex_color.lstrip("#")
    # An eight-digit value is #AARRGGBB in Qt. Only the opaque ones are
    # checked; a translucent overlay depends on what is behind it.
    if len(raw) == 8:
        raw = raw[2:]
    r, g, b = (int(raw[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def contrast(a: str, b: str) -> float:
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def to_lab(hex_color: str) -> tuple[float, float, float]:
    """CIE L*a*b*, for asking whether two colours look different.

    Contrast ratio is the wrong tool for that question: it measures lightness
    alone, so a red and a green of the same lightness score 1.0 and look
    nothing alike. Telling "it worked" from "it failed" is exactly that case.
    """
    raw = hex_color.lstrip("#")
    if len(raw) == 8:
        raw = raw[2:]
    r, g, b = (channel(int(raw[i:i + 2], 16)) for i in (0, 2, 4))

    # sRGB to CIE XYZ under D65, then XYZ to Lab.
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883

    def f(t: float) -> float:
        return t ** (1 / 3) if t > 0.008856 else (7.787 * t) + (16 / 116)

    fx, fy, fz = f(x), f(y), f(z)
    return (116 * fy) - 16, 500 * (fx - fy), 200 * (fy - fz)


def difference(a: str, b: str) -> float:
    """CIE76 colour difference. Roughly, 1 is the smallest change anyone can
    see side by side and 10 is unmistakable at a glance."""
    la, aa, ba = to_lab(a)
    lb, ab, bb = to_lab(b)
    return ((la - lb) ** 2 + (aa - ab) ** 2 + (ba - bb) ** 2) ** 0.5


def parse_palette() -> tuple[dict[str, str], dict[str, str]]:
    """Returns the light and dark palettes as name -> #RRGGBB."""
    light: dict[str, str] = {}
    dark: dict[str, str] = {}

    both = re.compile(
        r'property\s+color\s+(\w+):\s*dark\s*\?\s*"(#[0-9A-Fa-f]{6,8})"\s*:\s*"(#[0-9A-Fa-f]{6,8})"')
    single = re.compile(r'property\s+color\s+(\w+):\s*"(#[0-9A-Fa-f]{6,8})"')

    for line in COLORS_QML.read_text(encoding="utf-8").splitlines():
        if (match := both.search(line)) is not None:
            dark[match.group(1)] = match.group(2)
            light[match.group(1)] = match.group(3)
        elif (match := single.search(line)) is not None:
            dark[match.group(1)] = light[match.group(1)] = match.group(2)

    return light, dark


# (foreground, background, minimum, what it is)
PAIRS = [
    ("textPrimary", "background", TEXT, "body text on the window"),
    ("textPrimary", "surface", TEXT, "body text on a card"),
    ("textPrimary", "surfaceElevated", TEXT, "body text on a raised surface"),
    ("textPrimary", "surfaceSunken", TEXT, "body text on a sunken surface"),
    ("textPrimary", "surfaceHover", TEXT, "body text under the pointer"),
    ("textSecondary", "background", TEXT, "secondary text on the window"),
    ("textSecondary", "surface", TEXT, "secondary text on a card"),
    ("textSecondary", "surfaceHover", TEXT, "secondary text under the pointer"),

    # WCAG exempts the text of a switched-off control from its minimum. It is
    # checked anyway, at a lower bar, because docs/design.md section 10 asks
    # for readable disabled states and somebody still has to be able to tell
    # what the button would say if it were on.
    ("textDisabled", "surface", DISABLED_TEXT, "disabled text on a card"),
    ("textDisabled", "background", DISABLED_TEXT, "disabled text on the window"),

    ("textOnAccent", "accent", TEXT, "the label of a primary button"),
    ("textOnAccent", "accentHover", TEXT, "a primary button under the pointer"),
    ("textOnAccent", "accentPressed", TEXT, "a primary button being pressed"),
    ("accentText", "background", TEXT, "a link on the window"),
    ("accentText", "surface", TEXT, "a link on a card"),
    ("accentText", "accentSubtle", TEXT, "the label of a selected row"),

    ("accent", "background", UI, "the fill of a primary button against the window"),
    ("accent", "surface", UI, "the fill of a primary button on a card"),
    ("focusRing", "background", UI, "the focus ring on the window"),
    ("focusRing", "surface", UI, "the focus ring on a card"),

    ("borderStrong", "surface", UI, "the edge of an input"),
    ("borderStrong", "background", UI, "the edge of an input on the window"),

    ("success", "surface", TEXT, "success text"),
    ("success", "successSubtle", TEXT, "success text on its own tint"),
    ("warning", "surface", TEXT, "warning text"),
    ("warning", "warningSubtle", TEXT, "warning text on its own tint"),
    ("error", "surface", TEXT, "error text"),
    ("error", "errorSubtle", TEXT, "error text on its own tint"),
    ("info", "surface", TEXT, "informational text"),
    ("info", "infoSubtle", TEXT, "informational text on its own tint"),

]

# Borders that outline a tinted panel rather than a control. WCAG's 3:1 covers
# "visual information required to identify a component", and none of these are
# that - the fill already says the panel is there, and the border is what keeps
# section 12's "borders rather than shadows" from disappearing into it. So the
# bar is only that the edge is visible against the fill it surrounds.
DECORATIVE_BORDERS = [
    ("successBorder", "successSubtle", "the edge of a success message"),
    ("warningBorder", "warningSubtle", "the edge of a warning message"),
    ("errorBorder", "errorSubtle", "the edge of an error message"),
    ("infoBorder", "infoSubtle", "the edge of an informational message"),
    ("accentBorder", "accentSubtle", "the edge of a selected card"),
    ("border", "surface", "the line between two regions"),
]

# Colours that carry different meanings must not look the same, or the meaning
# is gone. Section 9 is explicit that the accent is not a semantic colour, and
# the palette used to have `info` set to exactly the accent value.
#
# Measured as a colour difference rather than a contrast ratio: two of these
# pairs differ almost entirely in hue, which a contrast ratio cannot see.
DISTINCT = [
    ("accent", "info", "the brand colour and 'informational'"),
    ("success", "warning", "'it worked' and 'be careful'"),
    ("warning", "error", "'be careful' and 'it failed'"),
    ("success", "error", "'it worked' and 'it failed'"),
    ("accent", "success", "the brand colour and 'it worked'"),
    ("accent", "error", "the brand colour and 'it failed'"),
]


def check(scheme: str, palette: dict[str, str]) -> list[str]:
    problems: list[str] = []

    for foreground, background, minimum, what in PAIRS:
        if foreground not in palette or background not in palette:
            problems.append(f"{scheme}: {foreground} or {background} is not in the palette")
            continue
        ratio = contrast(palette[foreground], palette[background])
        if ratio < minimum:
            problems.append(
                f"{scheme}: {what} is {ratio:.2f}:1, below {minimum}:1 "
                f"({foreground} {palette[foreground]} on {background} {palette[background]})")

    for foreground, background, what in DECORATIVE_BORDERS:
        ratio = contrast(palette[foreground], palette[background])
        if ratio < DECORATIVE_BORDER:
            problems.append(
                f"{scheme}: {what} is invisible ({ratio:.2f}:1, want {DECORATIVE_BORDER}:1)")

    for first, second, what in DISTINCT:
        delta = difference(palette[first], palette[second])
        if delta < DISTINGUISHABLE:
            problems.append(
                f"{scheme}: {what} are too close to tell apart "
                f"(difference {delta:.1f}, want {DISTINGUISHABLE}) - "
                f"{first} {palette[first]}, {second} {palette[second]}")

    return problems


def main() -> int:
    light, dark = parse_palette()
    if not light:
        print(f"could not read any colours from {COLORS_QML}", file=sys.stderr)
        return 2

    problems = check("light", light) + check("dark", dark)
    if problems:
        print("Contrast problems:\n", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        print(f"\n{len(problems)} pair(s) below the minimum.", file=sys.stderr)
        return 1

    print(f"Both schemes pass: {len(PAIRS)} contrast pairs, "
          f"{len(DECORATIVE_BORDERS)} borders, {len(DISTINCT)} distinctions, each checked twice.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
