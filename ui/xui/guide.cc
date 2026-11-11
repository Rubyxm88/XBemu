//
// xemu User Interface: XBemu guide (Help > XBemu
// Guide...)
//
// The package README, inside the app: topics on the left, the chosen one on
// the right. A topic can end with buttons that do what it describes, so a
// player does not have to find the menu item.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
#include "common.hh"
#include "guide.hh"
#include "actions.hh"
#include "main-menu.hh"
#include "versions.hh"
#include "game-cache.hh"
#include "xbcheck.hh"
#include "shader-cache.hh"
#include "viewport-manager.hh"
#include "font-manager.hh"
#include "../xemu-xbonline.h"
#include "../xemu-xbcheck.h"
#include "../xemu-diag.h"

#include <SDL3/SDL.h>

GuideWindow guide_window;

static const ImVec4 heading_color(0.42f, 0.80f, 0.30f, 1.0f);
static const ImVec4 xbox_green(0.063f, 0.486f, 0.063f, 1.0f);
static const ImVec4 xbox_green_hover(0.10f, 0.62f, 0.10f, 1.0f);
static const ImVec4 xbox_green_active(0.05f, 0.40f, 0.05f, 1.0f);

struct GuideAction {
    const char *label;
    void (*run)(void);
};

struct GuidePage {
    const char *title;
    // Paragraphs; a line starting with "# " is a heading, "- " a bullet,
    // "> " a menu path
    const char *const *text;
    GuideAction actions[3];
};

static void OpenGamesFolder() { xemu_xbcheck_open_games_folder(); }
static void OpenXblDashboard() { ActionXboxLiveSetup(); }
static void OpenLinkCode() { SDL_OpenURL(XEMU_XBONLINE_URL_LINK); }
static void OpenCreateAccount() { SDL_OpenURL(XEMU_XBONLINE_URL_CREATE); }
static void RunConnectionCheck() { xbcheck_window.Start(true); }
static void OpenShaderCache() { shader_cache_window.Open(); }
static void OpenGameCache() { game_cache_window.Open(); }
static void OpenLogs() { xemu_diag_open_folder(); }
static void OpenVersions() { versions_window.Open(); }
static void OpenSettings() { g_main_menu.ShowSettings(); }

static const char *const start_text[] = {
    "# First time",
#if defined(__APPLE__)
    "- Install with the Terminal line from README.txt (setup.sh). It puts "
    "the app in Applications and sets up your console.",
    "- Drag your game disc images (.iso) into the games folder that "
    "opens (Open Games Folder below).",
    "- Open XBemu from Applications. When macOS asks whether it "
    "may accept incoming network connections or find devices on your local "
    "network, click Allow: XB Online needs both.",
#elif defined(__linux__)
    "- Install with setup.sh (README.txt says how). It adds XBemu to "
    "your applications menu and sets up your console.",
    "- Copy your game disc images (.iso) into the games folder that "
    "opens (Open Games Folder below).",
    "- Start XBemu from your applications menu. On a Steam Deck, "
    "add it to Steam to play it in Game Mode (README.txt).",
#else
    "- Copy your game disc images (.iso) into the \"games\" folder.",
    "- Start XBemu. A setup window appears once: Set up my console "
    "makes a brand new console.",
#endif
    "- The XBL Dashboard installs itself (about a minute, once), then "
    "opens. Sign in with your xb.live account. No account "
    "yet? See Xbox Live Account first.",
    "- In the dashboard: Settings > Network, set the top option to XB Online.",
    "- Put a game in the tray, like pushing a disc into a real Xbox:",
    "> Play > Load <game>",
    "- Start it from the XBL Dashboard's Disc tile.",
    "# Every time after that",
    "Start XBemu and it opens the XBL Dashboard. Games always start from "
    "the dashboard, never straight from the menu.",
    NULL
};

static const char *const menus_text[] = {
    "# Machine",
    "Pause, screenshots, snapshots, BIOS, Settings... (controllers, audio "
    "device, network), Reset and Exit.",
    "# Play",
    "Load a game from the games folder into the tray, Load Disc and Eject "
    "Disc, and the XBL Dashboard.",
    "# XB Live",
    "Your Xbox Live account (opens xb.live), Create an Account, Link Code and "
    "the Connection Check.",
    "# Sound",
    "Sound quality (Fast Enhanced or Accurate), 5.1 surround and Louder "
    "movement.",
    "# Graphics",
    "Resolution, Enhanced graphics, fullscreen (F11), window scaling, "
    "shaders and the performance overlay.",
    "# Troubleshoot",
    "Everything for when something goes wrong: Connection Check, diagnostics "
    "log, freeze reports, the shader and game caches, the logs folder.",
    "# Help",
    "This guide, Versions & Updates and About. Click the build number on the "
    "right of the menu bar for Versions & Updates too.",
    "On a controller, press the Guide/Home button for the same options.",
#if defined(__APPLE__)
    "On a Mac laptop keyboard, F9 and the other F keys may need the fn key.",
#endif
    NULL
};

static const char *const account_text[] = {
    "Xbox Live accounts for XB Online are made on xb.live.",
    "- Create the account on xb.live/xb-online:",
    "> XB Live > Create an Account",
    "- Sign in to the XBL Dashboard and link the account. Link Code gives "
    "you the code:",
    "> XB Live > Link Code",
    "Your gamertag then shows at the top of the XB Live menu. In the game, "
    "sign in with the account the XBL Dashboard added.",
    NULL
};

static const char *const bios_text[] = {
    "XBemu runs OXOS, the open-source Xbox BIOS (github.com/kooscode/oxos). "
    "It comes with XBemu; no Microsoft files are included.",
    "Some disc games freeze on their loading screen with OXOS. XBemu has no "
    "other BIOS option.",
    "Game updates come through the XBL Dashboard when a game offers them.",
    NULL
};

static const char *const settings_text[] = {
    "# Graphics",
    "- Resolution: 1x (fastest) to 4x. 2x is the default.",
    "- Enhanced graphics: sharpens textures, distant floors and menu text, "
    "and smooths jagged edges.",
#if defined(__APPLE__)
    "- Renderer: always OpenGL on a Mac.",
#else
    "- Renderer: OpenGL (recommended) or Vulkan. Needs a restart. Shader "
    "caching works with OpenGL only.",
#endif
    "- Improve render handling: speeds up screen effects. Turn it off if "
    "effects look wrong.",
    "# Sound",
    "- Fast Enhanced (recommended): mixes the game's sounds straight to "
    "your speakers, with 3D direction fixes. Light on the PC.",
    "- Channels: Stereo, or 5.1 Surround for front, center, sub and "
    "rear speakers (Fast Enhanced only). 5.1 sets the Xbox's audio to Dolby "
    "Digital for you; no Xbox Dashboard needed.",
    "- Accurate: runs the Xbox's own sound effects processor, about a quarter "
    "of a CPU core more. Stereo only: it sets the Xbox's audio back to "
    "Stereo.",
    "- After changing either, choose Restart the game to apply in the Sound "
    "menu: the game reads the Xbox's audio setting when it starts.",
    "- Louder movement: makes footsteps and the rattle of gear and clothes "
    "easier to hear nearby.",
    "# Shaders",
    "The first time an effect appears (a map, night vision, a menu) your "
    "graphics card builds a shader for it, which can make a game hitch. "
    "XBemu keeps the shaders each game used and builds the "
    "missing ones in the background a few seconds after it starts. It happens "
    "again only after a graphics driver update or a resolution change.",
    "# Performance overlay",
    "Graphics > Performance Overlay > Show performance overlay: frame rate, "
    "1% lows, frame times, CPU, GPU and memory over the game.",
    NULL
};

static const char *const trouble_text[] = {
    "# \"Xbox Live not found\" or sign-in fails",
    "- Run the Connection Check and fix whatever it marks FIX or WARN first.",
    "- VPN: turn it off completely (also auto-connect and kill switch) and "
    "restart. A sign: the dashboard's Settings > Network shows an address "
    "that is not this PC's (for example 10.14.0.2).",
#if defined(__APPLE__)
    "- Clock: sign-in fails when the Mac's clock or time zone is wrong. Set "
    "both automatically in System Settings > General > Date & Time.",
    "- Local network: System Settings > Privacy & Security > Local Network "
    "must allow XBemu.",
#elif defined(__linux__)
    "- Clock: sign-in fails when the computer's clock or time zone is wrong. "
    "Set both automatically in your system's Date & Time settings.",
#else
    "- PC clock: sign-in fails when the Windows clock or time zone is wrong. "
    "Set both automatically in Windows Settings > Time.",
#endif
    "- A firewall, antivirus or router DNS setting can be in the way: allow "
    "XBemu through the firewall, or try another network.",
    "- Close any other emulator or game, then start again.",
#if defined(__APPLE__)
    "- Still failing: Troubleshoot > Make a Full Report (or double-click "
    "XBemu Report in Applications) and send the zip it makes in "
    "Downloads to whoever shared the package.",
#elif defined(__linux__)
    "- Still failing: Troubleshoot > Make a Full Report (or XBemu "
    "Report in your applications menu) and send the file it makes in "
    "Downloads to whoever shared the package.",
#else
    "- Still failing: send system\\xemu.log to whoever shared the package.",
#endif
    "# Game looks wrong",
    "Graphics: turn off Improve render handling. Effects missing or "
    "flickering: turn off Build shaders in the background in Graphics.",
    "# Slow or stuttering",
#if defined(__APPLE__)
    "Graphics > Resolution: 1x.",
#else
    "Graphics > Resolution: 1x. On a 144 Hz or 165 Hz monitor, set it to "
    "120 Hz or 60 Hz: the game runs at 30 frames.",
#endif
    "# Lag",
    "Troubleshoot > Record Diagnostics Log, play, and press F9 right after "
    "each lag spike. Stop it from the same menu. Send the log (logs folder) "
    "to whoever shared the package: it holds your PC's specs and frame "
    "times, not your account.",
    "# Freezes",
    "When the game shows no new picture for 10 seconds, a freeze report is "
    "saved by itself. Troubleshoot > Open Last Freeze Report.",
    "# A modded disc plays like the original",
    "The game keeps copies of disc files on the console's hard disk. They are "
    "emptied by themselves when you play a different disc of the same game; "
    "Troubleshoot > Empty Game Cache... empties them now.",
    NULL
};

static const char *const hosting_text[] = {
    "- Players on your own network (another Xbox or PC) can join as it is. "
#if defined(__APPLE__)
    "Allow incoming connections when macOS asks.",
#elif defined(__linux__)
    "If you run a firewall (ufw, firewalld), allow UDP port 3074.",
#else
    "Allow XBemu through Windows Firewall when Windows asks.",
#endif
    "- Players over the internet can join only if your router forwards UDP "
    "port 3074 to this PC.",
#if defined(__APPLE__)
    "- Only one copy of XBemu per Mac can host.",
#else
    "- Only one copy of XBemu per PC can host.",
#endif
    NULL
};

static const char *const versions_text[] = {
#if defined(__APPLE__)
    "When a newer build is out, a small panel in the top left says so a few "
    "seconds after the app starts. Update now downloads it, closes the app "
    "and opens it again on the new build within about a minute.",
#elif defined(__linux__)
    "When a newer build is out, a small panel in the top left says so a few "
    "seconds after XBemu starts. Update now downloads it, closes "
    "XBemu and opens it again on the new build.",
#else
    "When a newer build is out, XBemu offers it when you start.",
#endif
    "Help > Versions & Updates lists every build: install the newest, or go "
    "back to an earlier one if a new build gives you trouble. It asks first, "
    "then closes XBemu, installs the build and opens it again.",
    "Stable | Beta, at the top right of that window: Beta also offers beta "
    "builds before everyone else. They may have problems. "
    "Back on Stable you can return to the latest stable build right away.",
#if defined(__APPLE__) || defined(__linux__)
    "An update replaces the app only. Your console, Xbox Live sign-in, saves, "
    "settings and games are kept.",
#else
    "An update replaces the launcher, the emulator and the shader list only. "
    "Your console, Xbox Live sign-in, saves, settings and games are kept.",
#endif
    "Builds marked Broken can not be installed: they had a problem that was "
    "found after they came out.",
#if defined(__APPLE__)
    "If an update cannot be installed, the app says why the next time it "
    "opens. To update by hand, download XBemu-mac-update.zip from "
    "the releases page and run the Terminal line from README.txt.",
#elif defined(__linux__)
    "If an update cannot be installed, XBemu says why the next "
    "time it opens. To update by hand, download "
    "XBemu-linux-update.tar.gz from the releases page, unpack it "
    "and run its setup.sh.",
#else
    "To turn the check at start off, create an empty file "
    "system\\no-update-check.txt.",
#endif
    NULL
};

static const char *const files_text[] = {
#if defined(__APPLE__)
    "# Where things are",
    "- The app: Applications > XBemu (or Applications in your "
    "home folder, if setup said so).",
    "- Your console: ~/Library/Application Support/XBemu/system",
    "- Your games: ~/Library/Application Support/XBemu/games",
    "To open one in Finder: Go > Go to Folder..., paste the path, press "
    "Return.",
    "# Starting over",
    "Run the Terminal line from README.txt with --start-over at the end. Your "
    "current console, with its Xbox Live account and saves, is lost.",
    "# Do not share",
    "system/eeprom.bin and system/xbox_hdd.qcow2 belong to your console and "
    "your account. Never send them to anyone. Share the original zip instead.",
    "# Controller",
    "Pair it in Bluetooth settings before starting and it is used by itself. "
    "To change it: Machine > Settings... > Input.",
#elif defined(__linux__)
    "# Where things are",
    "- Everything: ~/.local/share/XBemu (a hidden folder in your "
    "home folder; Ctrl+H shows hidden folders in most file managers).",
    "- Your console: its system folder. Your games: its games folder.",
    "# Starting over",
    "Run setup.sh with --start-over at the end. Your current console, with "
    "its Xbox Live account and saves, is lost.",
    "# Do not share",
    "system/eeprom.bin and system/xbox_hdd.qcow2 belong to your console and "
    "your account. Never send them to anyone. Share the original download "
    "instead.",
    "# Controller",
    "Connect it before starting and it is used by itself. To change it: "
    "Machine > Settings... > Input.",
#else
    "# Starting over",
    "\"system\\Start over.bat\" removes the console in this folder. The next "
    "start asks again whether you want a clean start or your own files.",
    "# Do not share",
    "After setup, system\\eeprom.bin and system\\xbox_hdd.qcow2 belong to your "
    "console and your account. Never send them to anyone. Share the original "
    "zip instead.",
    "# Controller",
    "Plug a controller in before starting and it is used by itself. To change "
    "it: Machine > Settings... > Input.",
#endif
    NULL
};

static const GuidePage pages[] = {
    { "Getting Started", start_text,
      { { "Open Games Folder", OpenGamesFolder },
        { "Open XBL Dashboard", OpenXblDashboard } } },
    { "The Menus", menus_text, {} },
    { "Xbox Live Account", account_text,
      { { "Create an Account", OpenCreateAccount },
        { "Link Code", OpenLinkCode } } },
    { "BIOS & Game Updates", bios_text, {} },
    { "Graphics & Sound", settings_text,
      { { "Shader Cache...", OpenShaderCache } } },
    { "Troubleshooting", trouble_text,
      { { "Connection Check", RunConnectionCheck },
        { "Empty Game Cache...", OpenGameCache },
        { "Open Logs Folder", OpenLogs } } },
    { "Hosting a Game", hosting_text, {} },
    { "Versions & Updates", versions_text,
      { { "Versions & Updates...", OpenVersions } } },
    { "Your Files", files_text,
      { { "Machine Settings...", OpenSettings } } },
};

GuideWindow::GuideWindow()
{
    is_open = false;
    m_page = 0;
}

void GuideWindow::Open(int page)
{
    is_open = true;
    m_page = page;
    ImGui::SetWindowFocus("XBemu Guide");
}

static void DrawText(const char *const *text)
{
    float scale = g_viewport_mgr.m_scale;
    for (int i = 0; text[i]; i++) {
        const char *line = text[i];
        if (!strncmp(line, "# ", 2)) {
            if (i > 0) {
                ImGui::Dummy(ImVec2(0, 4 * scale));
            }
            ImGui::PushStyleColor(ImGuiCol_Text, heading_color);
            ImGui::TextUnformatted(line + 2);
            ImGui::PopStyleColor();
        } else if (!strncmp(line, "- ", 2)) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", line + 2);
        } else if (!strncmp(line, "> ", 2)) {
            // A menu path, set off like a key the player presses
            ImGui::Indent(14 * scale);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.92f, 0.80f, 1));
            ImGui::TextWrapped("%s", line + 2);
            ImGui::PopStyleColor();
            ImGui::Unindent(14 * scale);
        } else {
            ImGui::TextWrapped("%s", line);
        }
        ImGui::Dummy(ImVec2(0, 2 * scale));
    }
}

void GuideWindow::Draw()
{
    if (!is_open) {
        return;
    }

    float scale = g_viewport_mgr.m_scale;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(720 * scale, 470 * scale),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("XBemu Guide", &is_open,
                      ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    int count = (int)(sizeof(pages) / sizeof(pages[0]));
    if (m_page < 0 || m_page >= count) {
        m_page = 0;
    }

    ImGui::BeginChild("topics", ImVec2(190 * scale, 0), true);
    for (int i = 0; i < count; i++) {
        if (ImGui::Selectable(pages[i].title, m_page == i)) {
            m_page = i;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    const GuidePage &page = pages[m_page];
    float footer = page.actions[0].label ? ImGui::GetFrameHeightWithSpacing() +
                                               ImGui::GetStyle().ItemSpacing.y
                                         : 0;
    ImGui::BeginGroup();
    ImGui::BeginChild("page", ImVec2(0, -footer));
    ImGui::PushFont(g_font_mgr.m_menu_font_medium);
    ImGui::TextUnformatted(page.title);
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2 * scale));
    DrawText(page.text);
    ImGui::EndChild();

    if (page.actions[0].label) {
        ImGui::PushStyleColor(ImGuiCol_Button, xbox_green);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, xbox_green_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, xbox_green_active);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        for (int i = 0; i < 3 && page.actions[i].label; i++) {
            if (i > 0) {
                ImGui::SameLine();
            }
            if (ImGui::Button(page.actions[i].label)) {
                page.actions[i].run();
            }
        }
        ImGui::PopStyleColor(4);
    }
    ImGui::EndGroup();

    ImGui::End();
}
