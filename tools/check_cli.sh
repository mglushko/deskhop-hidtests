#!/usr/bin/env bash
# The replay tools' command lines, checked at full length only, so this passes on any
# tree the decode suites pass on: the entry selector, the decimal argument parse, the
# usage paths, and add_descriptor.py's refusals. Nothing here replays a truncated report
# or descriptor. That is what shortreport and truncate exist for, and their exit status
# is the finding, not a regression.
#
#   tools/check_cli.sh <build dir>
set -u
cd "$(dirname "$0")/.."
out=${1:?usage: check_cli.sh <build dir>}
fail=0

# expect <status> <pattern> <command...>: the command's combined output must contain the
# pattern and its exit status must be <status>. Stdin is whatever expect itself was given.
expect() {
    local want=$1 pattern=$2 got text
    shift 2
    text=$("$@" 2>&1)
    got=$?
    if [ "$got" -eq "$want" ] && grep -qF -- "$pattern" <<<"$text"; then
        printf '  ok    %s\n' "$*"
    else
        printf '  FAIL  %s\n        wanted status %s and "%s", got status %s:\n' "$*" "$want" "$pattern" "$got"
        printf '%s\n' "$text" | head -4 | sed 's/^/        | /'
        fail=1
    fi
}

# Lengths come from the tools, so no number here goes stale with the corpus: dump names
# the descriptor's size, and a length past the case makes shortreport say the range.
desc_len=$("$out/dump" boot_mouse | sed -n '1s/^boot_mouse (\([0-9]*\) bytes)$/\1/p')
mouse_len=$("$out/shortreport" mouse/boot_mouse/boot 0 999 2>&1 | sed -n 's/.* in [0-9]*\.\.\([0-9]*\)$/\1/p')
kbd_len=$("$out/shortreport" kbd/boot_keyboard/boot 0 999 2>&1 | sed -n 's/.* in [0-9]*\.\.\([0-9]*\)$/\1/p')
if [ -z "$desc_len" ] || [ -z "$mouse_len" ] || [ -z "$kbd_len" ]; then
    echo "  FAIL  could not read the lengths back from dump and shortreport"
    exit 1
fi

echo "shortreport, the entry selector"
expect 0 "kbd/boot_keyboard/boot case 0" "$out/shortreport" kbd/boot_keyboard/boot 0 "$kbd_len"
expect 0 "mouse/boot_mouse/boot case 0"  "$out/shortreport" boot_mouse 0 "$mouse_len"
expect 2 "kbd/boot_keyboard/boot"        "$out/shortreport" boot_keyboard 0 "$kbd_len"
expect 2 "no entry"                      "$out/shortreport" mouse/boot_keyboard 0 "$kbd_len"
expect 2 "no entry"                      "$out/shortreport" nosuch 0 1
expect 2 "usage:"                        "$out/shortreport" a b

echo "decimal arguments"
expect 0 "first $kbd_len of $kbd_len"    "$out/shortreport" kbd/boot_keyboard/boot 0 "0$kbd_len"
expect 2 "not a decimal number"                  "$out/shortreport" boot_mouse 0 0x5
expect 0 "first $desc_len of $desc_len"  "$out/truncate" boot_mouse "0$desc_len"
expect 2 "not a decimal number"                  "$out/truncate" boot_mouse 0x1

echo "usage paths"
expect 2 "usage:"                        "$out/truncate" boot_mouse
expect 2 "no descriptor named"           "$out/truncate" nosuch 1
expect 2 "not a C identifier"            python3 tools/add_descriptor.py 9bad </dev/null
expect 1 "Turn a pasted descriptor"      python3 tools/add_descriptor.py -x
expect 1 "no hex bytes"                  python3 tools/add_descriptor.py newname < <(printf 'nothing here\n')

if [ "$fail" -ne 0 ]; then
    echo "  a replay tool no longer answers its command line as documented"
    exit 1
fi
echo "  every command line answered as documented"
