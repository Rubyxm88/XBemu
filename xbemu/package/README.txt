XBEMU
=====
An original Xbox emulator built on xemu, set up for Xbox Live on XB Online,
with the XBL Dashboard and the open-source OXOS BIOS. No Microsoft files
are included.
Download and release notes: https://github.com/Rubyxm88/XBemu/releases/latest


FIRST TIME
----------
Installed with XBemu-win-...-setup.exe: XBemu is in the Start menu, and
"XBemu Games folder" next to it opens the games folder. From the zip: the
games folder and "XBemu" are in the folder you unzipped.

1. Copy your game disc images (.iso) into the "games" folder.
2. Start "XBemu". A setup window appears once: click
   "Set up my console". It makes a brand new console on OXOS.
3. The XBL Dashboard installs itself (about a minute, one time only), then
   opens. Sign in with your xb.live account.
   Need a code? Menu: XB Live > Link Code.
   No account yet? See NEW ACCOUNT below first.
4. In the dashboard: Settings > Network, set the top option to XB Online.
5. Menu: Play > Load <game>. The disc goes in the tray, like pushing a disc
   into a real Xbox. Start it from the XBL Dashboard's Disc tile.

All of this is also in the app: Help > XBemu Guide.


EVERY TIME AFTER THAT
---------------------
Double-click "XBemu". It starts in the XBL Dashboard. Load a game from the
Play menu and start it from the dashboard. Games always start from the
dashboard, never straight from the menu.


THE MENUS (top of the window)
-----------------------------
Each menu has one job. On a controller, press the Guide/Home button for the
same options.

MACHINE        the console itself
  Pause, Screenshot (F12), Snapshot, BIOS, Settings..., Reset, Exit

PLAY           what is in the disc drive
  GAMES
    Load <game>               Puts a game from the "games" folder in the tray.
                              No games yet: opens the games folder.
    Load Disc... / Eject Disc Any other disc image.
  DASHBOARD
    XBL Dashboard             Back to the dashboard.

XB LIVE        your Xbox Live account and the connection
  ACCOUNT
    Account: <gamertag>       The account on this console. Opens your
                              xb.live dashboard in the browser.
    Create an Account         Opens xb.live/xb-online (see NEW ACCOUNT).
    Link Code                 Opens xb.live/link in your browser.
  CONNECTION
    Connection Check          Runs the XB Online check (see below).

SOUND          Quality (Fast or Accurate), Channels (Stereo or 5.1), volume.

GRAPHICS       Resolution, Enhanced graphics, Fullscreen (F11), shaders,
               Performance Overlay, Renderer.

TROUBLESHOOT   Connection Check, Record Diagnostics Log (F9 marks a lag
               spike), freeze reports, Shader Cache, Empty Game Cache,
               Open Logs Folder.

HELP
  XBemu Guide...              This README, inside the app, with buttons.
  Versions & Updates...       Install a newer or an older build, and pick
                              Stable or Beta (see UPDATES).
  About

The build you have shows at the right of the menu bar. An amber dot on it
means a newer build is out; click it for Versions & Updates.


XBL DASHBOARD
-------------
XBemu has no other dashboard. The first time a console starts, the XBL
Dashboard installer (system\XBL Dashboard Installer.iso) runs once with a
progress bar and makes the XBL Dashboard the console's dashboard. It is
installed on the console's hard disk, so it updates itself when it offers
an update. Don't close the emulator while it installs.

XBL Dashboard and its installer are the work of their creator and are
included with their permission.


BIOS
----
XBemu runs OXOS, the open-source Xbox BIOS (github.com/kooscode/oxos,
GPL-2.0). It comes with XBemu in system\bios\. Some disc games freeze on it.
XBemu has no other BIOS option. What happened is in system\bios-log.txt.

New OXOS versions: XBemu looks once a day and asks before updating. Each
one says "Tested with XBemu" or "Not tested with XBemu yet". The download is
checked against the release's SHA-256.

Going back: Machine > BIOS lists the OXOS versions on this computer (the
last three are kept). Choose one and XBemu restarts with it. If XBemu is
closed soon after an update, the next start offers to go back. Your choice
stays until an XBemu update brings a newer tested version.


SHADERS
-------
The first time an effect appears your graphics card builds a shader for
it, which can make a game hitch. XBemu keeps the shaders each game used and
builds them again in the background a few seconds after the game starts.
To turn it off: Graphics > Precache > Precache shaders automatically.


LAG AND FREEZES
---------------
If a game stutters: Troubleshoot > Record Diagnostics Log, play as usual,
and press F9 right after each lag spike. If a game freezes (no new picture
for 10 seconds), a short freeze report is saved by itself in system\logs.
Send logs to whoever shared XBemu. They hold your PC's specs and frame
times, not your account.


GAME CACHE
----------
The Xbox keeps a copy of files a game reads from its disc on the console's
hard disk. When you play a changed disc image of the same game, XBemu
empties that copy by itself. Troubleshoot > Empty Game Cache... does it by
hand. Saves, accounts and game updates are not touched.


NEW ACCOUNT
-----------
Xbox Live accounts for XB Online are made on xb.live.
1. Menu: XB Live > Create an Account. It opens xb.live/xb-online in your
   browser; create the account there.
2. Then go on with step 3 of FIRST TIME: sign in to the XBL Dashboard and
   link the account (Link Code gives you the code).


UPDATES
-------
When a newer build is out, XBemu offers it when you start. An update
replaces the launcher, the emulator and the shader list only. Your console,
Xbox Live sign-in, saves, settings and games are not touched. The programs
it replaces go to system\backups (the last 3 kept).

Help > Versions & Updates... lists every build. Pick a newer one to update,
or an older one to go back if the newest gives you trouble.
Builds are named like "v0.1 build 7"; the number goes up with every build.
Stable | Beta, at the top right of that window: Stable gets stable builds
only. Beta also gets beta builds before everyone else; they may have
problems. Going back to Stable offers the latest stable build
right away, or you can stay on the beta until the next stable build.
To turn the check off, create an empty file system\no-update-check.txt.
Builds and notes: https://github.com/Rubyxm88/XBemu/releases


BLACK ARROW ONLINE ON THE SAME PC
---------------------------------
XBemu is a separate install with its own console. It never reads or
changes a Black Arrow Online folder, so both can stay on one PC. Sign in
to the XBL Dashboard in XBemu once, like on a new console. Don't run both
at the same time: they use the same network port.


CONTROLLER
----------
Plug in a controller before starting and it is used automatically.
To change it: Machine > Settings > Input.


STARTING OVER
-------------
"system\Start over.bat" removes the console in this folder. The next start
asks again whether you want a clean start or your own files.


UNINSTALL
---------
Installed with the setup: Windows Settings > Apps > Installed apps, XBemu,
Uninstall. It asks whether to delete your console and games too; choose No
to keep them for a later install. From the zip: delete the folder.


DO NOT SHARE
------------
After setup, system\eeprom.bin and system\xbox_hdd.qcow2 belong to your
console and your account. Never send them to anyone. Share the original zip.


XB ONLINE CHECK
---------------
A few seconds after starting, XBemu quietly checks what Xbox Live needs:
the console network, the XB Online server, this PC's clock and the Xbox
Live account. When everything is fine, "XB Online Connected" shows for a
moment. When something is wrong, a panel in the top left corner says what
and how to fix it. Run it any time from XB Live > Connection Check.


TROUBLE
-------
"Xbox Live not found" or sign-in fails:
  - Look at the XB Online check panel and fix what it marks FIX or WARN.
  - VPN: turn it off completely (also auto-connect and kill switch) and
    restart XBemu. A sign: Settings > Network in the dashboard shows an
    address that is not this PC's normal one (for example 10.14.0.2).
  - PC clock: Settings > Time in Windows, set time and time zone
    automatically.
  - XB Online server: a firewall, antivirus, VPN or router DNS setting is in
    the way. Allow xemu through the firewall, or try another network.
  - Close any other emulator, then start XBemu again.
  - Still failing: send system\xemu.log to whoever shared XBemu.
A game freezes on its loading screen: it may not run on OXOS (see BIOS).
Game looks wrong: Graphics, turn off "Improve render handling".
Slow: Graphics > Resolution, choose 1x.
Hosting a game over the internet: your router must forward UDP port 3074
  to this PC. Only one copy of XBemu per PC can host.
