# XBemu

An original Xbox emulator for Windows, Mac and Linux, set up for Xbox Live
on [XB Online](https://xb.live). XBemu is a build of [xemu](https://xemu.app)
with a launcher that sets up a new console for you, the open-source
[OXOS](https://github.com/kooscode/oxos) BIOS and the XBL Dashboard.
No Microsoft files are included.

## Get started (Windows)

1. Download `XBemu-win-<build>-full.zip` from the
   [latest release](../../releases/latest) and unzip it anywhere.
2. Copy your game disc images (`.iso`) into the `games` folder.
3. Double-click **XBemu** and click **Set up my console**.
4. The XBL Dashboard installs itself (about a minute, once), then opens.
   Sign in with your xb.live account.
5. Menu: **Play > Load &lt;game&gt;** puts the disc in the tray. Start it
   from the dashboard.

The README in the package, and **Help > XBemu Guide** in the app, cover the
menus, accounts, the connection check and troubleshooting.

Mac and Linux: each release has `XBemu-mac-update.zip` and
`XBemu-linux-update.tar.gz` with a `setup.sh`.

## Builds and channels

Builds are named **v0.1 build N**. The build number goes up by one with
every release; the version (0.1) changes for bigger steps.

| Channel | What it is | Who gets it |
|---|---|---|
| **Stable** | The latest release | Everyone |
| **Beta** | A pre-release, before it becomes stable | Players who pick **Beta** in Help > Versions & Updates |

XBemu checks for a newer build when it starts and offers it. Help >
Versions & Updates lists every build, so you can also go back to an older
one. Updates never touch your console, sign-in, saves, settings or games.

## Building and releasing

See [xbemu/README.md](xbemu/README.md).

## License

XBemu, like xemu and QEMU, is licensed under the GNU GPL, version 2 or later
(`LICENSE`, `COPYING`). OXOS is GPL-2.0. The XBL Dashboard and its installer
are the work of their creator, included with their permission.
