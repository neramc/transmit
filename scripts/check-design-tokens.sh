#!/usr/bin/env bash
#
# Rejects design values written straight into a component.
#
# docs/design.md section 37 asks for every design value to be centralised, and
# section 42 rule 1 forbids introducing a colour, radius, spacing or type style
# without first checking whether a token already says it. Neither can be
# enforced by review alone: a hard-coded 13 in one page looks like nothing, and
# a hundred of them are why an interface stops being consistent.
#
# What is allowed, and where:
#   src/ui/qml/theme/     the tokens themselves - this is where values live
#   a line ending in      // token-exempt: <reason>
#
# Nothing else in src/ui/qml may write a colour literal, or a number for a
# margin, padding, spacing, radius or font size.

set -uo pipefail

cd "$(dirname "$0")/.."

qml_files=$(git ls-files 'src/ui/qml/*.qml' | grep -v '^src/ui/qml/theme/')
if [ -z "$qml_files" ]; then
    echo "::error::no QML files found - the glob is wrong"
    exit 1
fi

failures=0

report() {
    failures=$((failures + 1))
    printf '%s\n' "$1"
}

# One pass per rule, so the message can say what is actually wrong rather than
# "something on this line".
scan() {
    local pattern="$1" what="$2" advice="$3"
    local hits
    hits=$(grep -nE "$pattern" $qml_files | grep -v 'token-exempt:' || true)
    if [ -n "$hits" ]; then
        while IFS= read -r hit; do
            report "  $hit"
        done <<< "$hits"
        report "^^^ $what. $advice"
        report ""
    fi
}

echo "Checking $(echo "$qml_files" | wc -l) QML files for values that should be tokens..."
echo

scan '"#[0-9A-Fa-f]{3,8}"' \
     'a colour written into a component' \
     'Use a Colors token, or add one if the meaning is genuinely new.'

scan '\b(margins|topMargin|bottomMargin|leftMargin|rightMargin|padding|topPadding|bottomPadding|leftPadding|rightPadding|spacing|columnSpacing|rowSpacing):[[:space:]]*[1-9][0-9]*' \
     'a spacing value written into a component' \
     'Use a Spacing token: Spacing.s4 through Spacing.s64, or one of the role aliases.'

scan '\bradius:[[:space:]]*[0-9]+' \
     'a corner radius written into a component' \
     'Use a Radius token: Radius.chip, control, card, dialog or pill.'

scan '\bfont\.(pixelSize|pointSize):[[:space:]]*[0-9]+' \
     'a font size written into a component' \
     'Use a Typography token: display, pageTitle, sectionTitle, body, secondary or caption.'

scan '\bfont\.(weight|bold):[[:space:]]*(Font\.[A-Za-z]+|true)' \
     'a font weight written into a component' \
     'Use Typography.regular, medium, semiBold or bold.'

scan '\bfont\.family:[[:space:]]*"' \
     'a font family written into a component' \
     'Use Typography.family or Typography.monoFamily.'

# Animations that do not pass through Motion.duration() ignore the "reduce
# motion" preference, which makes the setting a lie. Section 28 and the
# accessibility section both depend on it.
#
# Both a bare number and a token used without the wrapper are caught, because
# Motion.duration() is the thing that returns 0 when the preference is set -
# "duration: Motion.hover" animates at full length however it is set.
hits=$(grep -nE '\bduration:[[:space:]]*([0-9]+|Motion\.[A-Za-z]+([^(A-Za-z]|$))' $qml_files \
       | grep -v 'token-exempt:' || true)
if [ -n "$hits" ]; then
    while IFS= read -r hit; do report "  $hit"; done <<< "$hits"
    report "^^^ an animation that ignores the reduce-motion setting."
    report "    Use Motion.duration(Motion.hover) and friends, never a bare number."
    report ""
fi

if [ "$failures" -gt 0 ]; then
    echo
    echo "::error::design values are hard-coded in $failures place(s); see above"
    exit 1
fi

echo "Every design value in the QML comes from a token."
