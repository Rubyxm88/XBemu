#!/usr/bin/env bash
# Writes a published release's notes (Markdown) to stdout: what to download
# first, then what changed, then the files players don't need.
#
#   release-notes.sh VERSION_TXT CHANGES_MD ASSET_NAME...
#
# VERSION_TXT  the Windows version.txt of the release (build, release,
#              channel, xemu, version lines)
# CHANGES_MD   "- " lines for "What changed" (may be missing or empty)
# ASSET_NAME   every file the release has (or will have once uploaded)
#
# Environment: GITHUB_REPOSITORY, TAG, IMAGES_REF (commit or branch the
# pictures are read from); optional SYSTEMS ("windows mac linux" when all were
# built) and PREV_TAG for a partial build.
set -euo pipefail

vt=$1
changes=$2
shift 2
assets=("$@")

field() { { grep -m 1 "^$1=" "$vt" 2>/dev/null || true; } | cut -d= -f2- | tr -d '\r'; }
build=$(field build)
release=$(field release)
channel=$(field channel)
xemu=$(field xemu)
version=$(field version)
label="v${release:-0.1} build $build"

repo=$GITHUB_REPOSITORY
dl="https://github.com/$repo/releases/download/$TAG"
images="https://raw.githubusercontent.com/$repo/${IMAGES_REF:-main}/xbemu/docs/images"

# First asset whose name matches a pattern
pick() {
    local a
    for a in "${assets[@]}"; do
        # shellcheck disable=SC2254
        case "$a" in $1) printf '%s' "$a"; return 0 ;; esac
    done
    return 0
}
link() { printf '[%s](%s/%s)' "$1" "$dl" "$1"; }

win_full=$(pick 'XBemu-win-*-full.zip')
win_setup=$(pick 'XBemu-win-*-setup.exe')
mac_full=$(pick 'XBemu-mac-*-full.zip')
linux_full=$(pick 'XBemu-linux-*-full.tar.gz')
source=$(pick 'XBemu-source-*.tar.zst')

echo "**XBemu $label**, based on xemu ${xemu:-?}${version:+ (source version $version)}."
echo
if [ "$channel" = beta ]; then
    echo "> **Beta build.** Only players on Beta (the Stable | Beta switch in Help > Versions & Updates, or in the update window) are offered it."
    echo
fi
if [ -n "${SYSTEMS:-}" ] && [ "$SYSTEMS" != "windows mac linux" ]; then
    echo "<!-- systems: $SYSTEMS -->"
    echo "> For $(echo "$SYSTEMS" | sed 's/windows/Windows/; s/mac/Mac/; s/linux/Linux/; s/ /, /g') only. Other systems keep the files of build ${PREV_TAG#xbemu-} and are not offered anything."
    echo
fi

echo "## Download"
echo
if [ -n "$win_setup$win_full$mac_full$linux_full" ]; then
    echo "**New to XBemu?** Download **one** file, the one for your computer. It has everything to start: the app, the open-source OXOS BIOS, the XBL Dashboard installer and a new blank console. No Microsoft files."
    echo
    echo "| Your computer | Download | Then |"
    echo "|---|---|---|"
    if [ -n "$win_setup" ]; then
        echo "| **Windows** 10 or 11 | $(link "$win_setup") | Run it and click Install (no admin needed). XBemu starts; copy your game discs (.iso) into its games folder (Start menu > XBemu Games folder) |"
    elif [ -n "$win_full" ]; then
        echo "| **Windows** 10 or 11 | $(link "$win_full") | Unzip it anywhere, copy your game discs (.iso) into \`games\`, double-click **XBemu** |"
    fi
    if [ -n "$mac_full" ]; then
        echo "| **Mac**, Apple Silicon (macOS 14+) or Intel (macOS 11+) | $(link "$mac_full") | Unzip it in Downloads (double-click it if your browser did not), then paste the Mac line below into Terminal |"
    fi
    if [ -n "$linux_full" ]; then
        echo "| **Linux** or Steam Deck (x86_64) | $(link "$linux_full") | Paste the Linux line below into a terminal (Steam Deck: Konsole, in Desktop Mode) |"
    fi
    echo
    # The lines from the Mac and Linux READMEs: each installs and starts XBemu
    if [ -n "$mac_full" ]; then
        echo "**Mac line** (Cmd+Space, type Terminal, press Return, paste with Cmd+V, press Return). It installs XBemu and starts it:"
        echo
        echo '```'
        echo 'bash "$(ls -td ~/Downloads/XBemu*/ | head -n 1)setup.sh"'
        echo '```'
        echo
    fi
    if [ -n "$linux_full" ]; then
        echo "**Linux line** (paste with Ctrl+Shift+V, press Enter). It unpacks the download, installs XBemu and starts it:"
        echo
        echo '```'
        echo 'cd ~/Downloads && tar -xzf "$(ls -t XBemu-linux-*-full.tar.gz | head -n 1)" && bash XBemu/setup.sh'
        echo '```'
        echo
    fi
    if [ -n "$win_setup" ] && [ -n "$win_full" ]; then
        echo "Windows without installing: $(link "$win_full") is the same XBemu in a folder. Unzip it anywhere and double-click **XBemu**."
        echo
    fi
fi
echo "**Already have XBemu?** Don't download anything. Start XBemu as usual: it offers this build and installs it in a few seconds. Your console, Xbox Live sign-in, saves, settings and games are not touched."
echo
echo "<img src=\"$images/launcher-update.png\" width=\"420\" alt=\"The XBemu update window\">"
echo
echo "Step-by-step help: the [README](https://github.com/$repo#readme), or Help > XBemu Guide in the app."
echo

if [ -s "$changes" ]; then
    echo "## What changed"
    echo
    cat "$changes"
    echo
fi

echo "<details>"
echo "<summary><b>Other files</b> (for the updater, updating by hand, and the source code). You don't need these to play.</summary>"
echo
echo "| File | What it is |"
echo "|---|---|"
if [ -n "$(pick 'XBemu-update.zip')" ]; then
    echo "| \`XBemu-update.zip\` | Windows update: close XBemu and unzip it over your XBemu folder |"
fi
if [ -n "$(pick 'XBemu-mac-update.zip')" ]; then
    echo "| \`XBemu-mac-update.zip\` | Mac update: unzip it in Downloads and paste the Terminal line from its README.txt |"
fi
if [ -n "$(pick 'XBemu-linux-update.tar.gz')" ]; then
    echo "| \`XBemu-linux-update.tar.gz\` | Linux update: extract it and run \`bash setup.sh\` in the folder |"
fi
vfiles=""
for f in version.txt mac-version.txt linux-version.txt; do
    if [ -n "$(pick "$f")" ]; then
        vfiles="$vfiles${vfiles:+, }\`$f\`"
    fi
done
if [ -n "$vfiles" ]; then
    echo "| $vfiles | What XBemu reads to find this build |"
fi
if [ -n "$source" ]; then
    echo "| \`$source\` | The complete source this build was made from, with xemu's submodules |"
fi
echo "| Source code (zip, tar.gz) | Added by GitHub to every release: this repository only, without xemu's submodules |"
echo
echo "</details>"
