# XBemu: building and releasing

This repository is the xemu source with XBemu's changes on top (the commits
after the xemu release tag), plus the launcher and packaging in this folder.

## Folders

| Folder | Contents |
|---|---|
| `VERSION` | The version line of the builds, such as `0.1` |
| `package/` | Launcher source (`Launcher.cs`), the player README, the XBL Dashboard installer disc, the shader list, `oxos.txt` (the OXOS release used) |
| `tools/` | Package and blank-disk scripts, FATX helpers, Docker build scripts |
| `mac/`, `linux/` | Setup scripts of the Mac and Linux apps |
| `docs/` | Images for the README and release notes |

## What is never in this repository or a release

Microsoft's BIOS and MCPX boot ROM, the Microsoft dashboard, used console
disks (`eeprom.bin`, a played `xbox_hdd.qcow2`), game discs and game
updates. The packaging stops when one of them turns up.

## Releases

`.github/workflows/xbemu.yml` builds Windows, Mac and Linux on every push
to `main`. **A push publishes nothing** unless its head commit message has a
channel line:

```
Fix the menu lag

Release-Channel: beta
```

| Line | Result |
|---|---|
| (none) | Builds everything; the files are kept as the run's Artifacts |
| `Release-Channel: beta` | Publishes `xbemu-N` as a **pre-release**: offered only to players on Beta |
| `Release-Channel: stable` | Publishes `xbemu-N` as the **latest** release: offered to everyone |
| `Release-Systems: mac linux` | With a channel: builds only those systems; the others keep the newest release's files |
| `Change: ...` | A line of its own in the release notes ("What changed") |

The same can be done by hand: Actions > XBemu > Run workflow, with a
channel. N is the next build number (the newest release + 1), and the title
is "XBemu v0.1 build N" ("... beta" for a beta).

Each release has `XBemu-win-N-full.zip` (the whole package for new
players), `XBemu-update.zip` + `version.txt` (what the launcher updates
from), the Mac and Linux update files, and the source.

### Deployments and approval

Publishing runs through the GitHub **environment** named after the channel,
`beta` or `stable`, so every release shows under Deployments. Both are made
on first use. To require an approval before a stable release goes out:
Settings > Environments > `stable` > Required reviewers. Deployment branches
(`main` only) can be set there too.

### Making a beta stable

Actions > **XBemu promote** > Run workflow, with the build number. The same
files become the latest release: the version files say `channel=stable`,
the pre-release mark and " beta" in the title go. It runs in the `stable`
environment, so it needs the same approval.

### Withdrawing a build

Turn the release back into a draft, or add a line `Broken: why` to its
notes: the app shows it greyed out and never offers it.

### New xemu releases

Once a day the workflow looks for a new xemu release and, when main does not
have it, makes a draft test build with it merged. Nothing is published; to
take it, merge the xemu tag into main by hand. xemu's own workflows are not
in this repository: when a merge brings one back (a modify/delete
conflict), remove it again with `git rm`.

## version.txt

```
build=7
release=0.1
channel=beta
xemu=0.8.139
version=0.8.139-12-g0123456789
date=2026-11-10
commit=<full hash>
upstream=v0.8.139
notes=<first line of the notes>
change=<one per line>
sha256=<of the update zip; release copy only>
```

Older numbering is still read (Black Arrow Online folders): `sub=` and
`beta=` lines, tags such as `xbemu-26.1` and `xbemu-27.3.1`.

## Building on a PC

`xemu.exe`, with Docker (builds the committed `xbemu-main`):

```
docker run --rm -v xemu-build:/work -v "<checkout>:/src:ro" -v "<out>:/out" \
    -v "<tools folder>:/tools:ro" \
    ghcr.io/xemu-project/xemu-win64-toolchain-gcc:sha-2881edd \
    bash /tools/docker-build-xbemu.sh
```

The launcher, with the .NET Framework's C# compiler:

```
csc.exe /nologo /target:winexe /optimize+ /win32icon:ui\icons\xbemu.ico ^
    /resource:data\xbemu_logo.png,xbemu_logo.png /out:XBemu.exe ^
    /r:System.Windows.Forms.dll /r:System.Drawing.dll ^
    /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll ^
    xbemu\package\Launcher.cs
```

The full package: `tools/make-blank-hdd.py` (then `qemu-img convert -O
qcow2`) for the blank disk, then `tools/make-xbemu-package.py` (see its
usage line; `--channel beta` for a beta).

## Package layout

```
XBemu.exe                       launcher
README.txt
games\                          the player's disc images
system\
    xemu.exe, LICENSE.txt
    version.txt
    bios\oxos-v0.4.4.bin        OXOS
    XBL Dashboard Installer.iso runs once on a new console
    shader-list.bin
    fresh\xbox_hdd.qcow2        blank disk for a new console
```
