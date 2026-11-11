#!/usr/bin/env bash
# Turns a Mac or Linux update folder into a full package folder: adds the
# system/ files setup.sh installs on a new computer (a content.txt mark, the
# OXOS BIOS and XBemu's own blank console disk). setup.sh makes the games
# folder itself, so the package has none.
#
#   add-full-content.sh PACKAGE_DIR BLANK_QCOW2 OXOS_TXT
#
# OXOS (GPL-2.0) comes from its GitHub release and is checked against the
# sha256 in oxos.txt. Nothing added is Microsoft's or a used console's.
set -euo pipefail

pkg=$1
blank=$2
oxos_txt=$3

oxos() { sed -n "s/^$1=//p" "$oxos_txt" | tr -d '\r'; }
tag=$(oxos tag)
[ -n "$tag" ] || { echo "no tag= in $oxos_txt" >&2; exit 1; }

mkdir -p "$pkg/system/bios" "$pkg/system/fresh"
cp "$oxos_txt" "$pkg/system/oxos.txt"
tmp=$(mktemp -d)
curl -fsSL -o "$tmp/oxos.zip" \
    "https://github.com/kooscode/oxos/releases/download/$tag/$(oxos zip)"
unzip -p "$tmp/oxos.zip" "$(oxos file)" > "$pkg/system/bios/oxos-$tag.bin"
rm -rf "$tmp"
echo "$(oxos sha256)  $pkg/system/bios/oxos-$tag.bin" | sha256sum -c -
cp "$blank" "$pkg/system/fresh/xbox_hdd.qcow2"
# setup.sh installs system/ from a folder only when this mark is there
printf 'XBemu full package: OXOS %s and a blank console disk\n' "$tag" \
    > "$pkg/system/content.txt"

bad=$(find "$pkg" -type f | sed "s|^$pkg/||" |
      grep -iE 'complex_4627|mcpx|eeprom\.bin|xboxdash|xbl dashboard(\.| loader)|black arrow|rainbow six|xbox_hdd\.qcow2' |
      grep -v '^system/fresh/xbox_hdd\.qcow2$' || true)
if [ -n "$bad" ]; then
    echo "::error title=File that must never ship::$bad"
    exit 1
fi
