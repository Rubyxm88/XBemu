//
// xemu User Interface: OXOS BIOS updates
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
#include "common.hh"
#include "oxos-update.hh"
#include "actions.hh"
#include "font-manager.hh"
#include "viewport-manager.hh"
#include "../xemu-settings.h"
#include "../xemu-notifications.h"
#include "../xbemu-brand.h"
#include "qemu/http.h"
#include "util/miniz/miniz.h"
#include <nlohmann/json.hpp>
#include <glib/gstdio.h>
#include <algorithm>
#include <stdlib.h>

using json = nlohmann::json;

OxosWindow oxos_window;

// Where the releases are looked up. Tests point these at a local server.
static const char *const releases_api =
    "https://api.github.com/repos/kooscode/oxos/releases?per_page=10";
// Which versions were tried with XBemu: a line per version, such as
// "v0.4.4 tested" or "v0.4.5 broken: Halo freezes on loading"
static const char *const versions_list =
    "https://raw.githubusercontent.com/" XBEMU_REPO
    "/main/xbemu/oxos-versions.txt";

static const double check_every = 20 * 60 * 60;  // seconds
static const double boot_ok_after = 120;          // seconds running
static const int keep_versions = 3;

static const ImVec4 good_color(0.45f, 0.85f, 0.45f, 1.0f);
static const ImVec4 warn_color(0.95f, 0.75f, 0.30f, 1.0f);
static const ImVec4 bad_color(0.95f, 0.45f, 0.40f, 1.0f);

static const char *Env(const char *name, const char *fallback)
{
    const char *v = getenv(name);
    return v && *v ? v : fallback;
}

static std::string BiosDir()
{
    return std::string(xemu_settings_get_base_path()) + "bios";
}

static std::string BiosFile(const std::string &tag)
{
    return BiosDir() + "/oxos-" + tag + ".bin";
}

// "v0.4.10" -> {0, 4, 10}
static std::vector<int> Ver(const std::string &tag)
{
    std::vector<int> v;
    int n = -1;
    for (char c : tag) {
        if (c >= '0' && c <= '9') {
            n = (n < 0 ? 0 : n * 10) + (c - '0');
        } else if (n >= 0) {
            v.push_back(n);
            n = -1;
        }
    }
    if (n >= 0) {
        v.push_back(n);
    }
    return v;
}

static bool Newer(const std::string &a, const std::string &b)
{
    return Ver(a) > Ver(b);
}

static bool IsTag(const std::string &tag)
{
    if (tag.size() < 2 || tag[0] != 'v' || tag.size() > 32) {
        return false;
    }
    for (char c : tag) {
        if (!g_ascii_isalnum(c) && c != '.' && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

// key=value files: the player's choice, oxos.txt
typedef std::map<std::string, std::string> Keys;

static Keys ReadKeys(const std::string &path)
{
    Keys keys;
    gchar *text = NULL;
    if (!g_file_get_contents(path.c_str(), &text, NULL, NULL)) {
        return keys;
    }
    gchar **lines = g_strsplit(text, "\n", -1);
    for (gchar **l = lines; *l; l++) {
        g_strstrip(*l);
        const char *eq = strchr(*l, '=');
        if (eq && eq != *l) {
            keys[std::string(*l, eq - *l)] = eq + 1;
        }
    }
    g_strfreev(lines);
    g_free(text);
    return keys;
}

static void WriteKeys(const std::string &path, const Keys &keys,
                      const char *header)
{
    std::string text = header;
    for (auto &kv : keys) {
        if (!kv.second.empty()) {
            text += kv.first + "=" + kv.second + "\n";
        }
    }
    g_file_set_contents(path.c_str(), text.c_str(), -1, NULL);
}

static std::string ChoicePath()
{
    return BiosDir() + "/oxos-choice.txt";
}

static Keys Choice()
{
    return ReadKeys(ChoicePath());
}

static void SaveChoice(const Keys &keys)
{
    g_mkdir_with_parents(BiosDir().c_str(), 0755);
    WriteKeys(ChoicePath(), keys,
              "# The OXOS version this player chose in Machine > BIOS (tag).\n"
              "# It stays until an XBemu update brings a newer tested one.\n");
}

// The version this XBemu package was tested with (system/oxos.txt)
static std::string PackageTag()
{
    Keys pin = ReadKeys(std::string(xemu_settings_get_base_path()) +
                        "oxos.txt");
    return pin["tag"];
}

// The version the console runs now, from the BIOS setting
static std::string CurrentTag()
{
    const char *path = g_config.sys.files.flashrom_path;
    if (!path) {
        return "";
    }
    gchar *base = g_path_get_basename(path);
    std::string name = base;
    g_free(base);
    if (name.size() > 9 && name.compare(0, 5, "oxos-") == 0 &&
        name.compare(name.size() - 4, 4, ".bin") == 0) {
        return name.substr(5, name.size() - 9);
    }
    return "";
}

// OXOS versions on this computer, newest first
static std::vector<std::string> LocalTags()
{
    std::vector<std::string> tags;
    GDir *dir = g_dir_open(BiosDir().c_str(), 0, NULL);
    if (!dir) {
        return tags;
    }
    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        std::string n = name;
        if (n.size() > 9 && n.compare(0, 5, "oxos-") == 0 &&
            n.compare(n.size() - 4, 4, ".bin") == 0) {
            std::string tag = n.substr(5, n.size() - 9);
            if (IsTag(tag)) {
                tags.push_back(tag);
            }
        }
    }
    g_dir_close(dir);
    std::sort(tags.begin(), tags.end(),
              [](const std::string &a, const std::string &b) {
                  return Newer(a, b);
              });
    return tags;
}

// The tested list, as last fetched (kept for when GitHub cannot be reached)
static std::string NotesPath()
{
    return BiosDir() + "/oxos-versions.txt";
}

static std::map<std::string, std::string> ParseNotes(const char *text)
{
    std::map<std::string, std::string> notes;
    gchar **lines = g_strsplit(text, "\n", -1);
    for (gchar **l = lines; *l; l++) {
        g_strstrip(*l);
        if (!**l || **l == '#') {
            continue;
        }
        gchar *sp = strpbrk(*l, " \t");
        if (!sp) {
            continue;
        }
        std::string tag(*l, sp - *l);
        std::string note = g_strstrip(sp + 1);
        if (IsTag(tag)) {
            notes[tag] = note;
        }
    }
    g_strfreev(lines);
    return notes;
}

static std::map<std::string, std::string> LocalNotes()
{
    gchar *text = NULL;
    std::map<std::string, std::string> notes;
    if (g_file_get_contents(NotesPath().c_str(), &text, NULL, NULL)) {
        notes = ParseNotes(text);
        g_free(text);
    }
    return notes;
}

static bool IsBroken(const std::map<std::string, std::string> &notes,
                     const std::string &tag, std::string *why = NULL)
{
    auto it = notes.find(tag);
    if (it == notes.end() || it->second.compare(0, 6, "broken") != 0) {
        return false;
    }
    if (why) {
        size_t colon = it->second.find(':');
        why->clear();
        if (colon != std::string::npos) {
            gchar *t = g_strstrip(g_strdup(it->second.c_str() + colon + 1));
            *why = t;
            g_free(t);
        }
    }
    return true;
}

static bool IsTested(const std::map<std::string, std::string> &notes,
                     const std::string &tag)
{
    auto it = notes.find(tag);
    if (it != notes.end()) {
        return it->second.compare(0, 6, "tested") == 0;
    }
    return tag == PackageTag();
}

// Keeps the newest few, plus the ones in use, chosen or tested
static void Prune(const std::vector<std::string> &keep_also)
{
    std::vector<std::string> tags = LocalTags();
    for (size_t i = keep_versions; i < tags.size(); i++) {
        if (std::find(keep_also.begin(), keep_also.end(), tags[i]) !=
            keep_also.end()) {
            continue;
        }
        g_remove(BiosFile(tags[i]).c_str());
        fprintf(stderr, "oxos: removed %s\n", tags[i].c_str());
    }
}

// Makes this version the console's BIOS (from the next start)
static void UseTag(const std::string &tag, bool is_new)
{
    std::string old = CurrentTag();
    std::string rel = "bios/oxos-" + tag + ".bin";
    xemu_settings_set_string(&g_config.sys.files.flashrom_path, rel.c_str());
    // OXOS starts without the MCPX boot ROM
    xemu_settings_set_string(&g_config.sys.files.bootrom_path, "");
    xemu_settings_save();

    Keys c = Choice();
    c["tag"] = tag;
    c["pin"] = PackageTag();
    // A version never run here: watched until it has run for a while
    if (is_new && !old.empty() && old != tag) {
        c["pending"] = tag;
        c["previous"] = old;
        c["starts"] = "0";
    } else {
        c.erase("pending");
        c.erase("previous");
        c.erase("starts");
    }
    SaveChoice(c);
    Prune({ tag, old, PackageTag() });
    fprintf(stderr, "oxos: %s chosen (was %s)\n", tag.c_str(), old.c_str());
}

static std::string Sha256(const guint8 *data, gsize len)
{
    gchar *hex = g_compute_checksum_for_data(G_CHECKSUM_SHA256, data, len);
    std::string s = hex;
    g_free(hex);
    return s;
}

OxosWindow::OxosWindow()
    : m_status(IDLE), m_progress(0), m_started(false), m_asked(false),
      m_start_time(-1), m_ran(0), m_prompt(NONE)
{
}

void OxosWindow::StartCheck(bool asked)
{
    int s = m_status;
    if (s == CHECKING || s == DOWNLOADING) {
        return;
    }
    m_asked = asked;
    m_status = CHECKING;
    g_thread_unref(g_thread_new("oxos-check", CheckThread, this));
}

void OxosWindow::CheckNow()
{
    StartCheck(true);
}

gpointer OxosWindow::CheckThread(gpointer opaque)
{
    OxosWindow *self = (OxosWindow *)opaque;
    std::vector<Release> releases;
    Error *err = NULL;

    // Which versions were tried with XBemu
    GByteArray *list = g_byte_array_new();
    int code = http_get(Env("XBEMU_OXOS_LIST", versions_list), list, NULL,
                        &err);
    if (err) {
        error_free(err);
        err = NULL;
    }
    std::map<std::string, std::string> notes;
    if (code == 200) {
        g_byte_array_append(list, (const guint8 *)"", 1);
        notes = ParseNotes((const char *)list->data);
        g_mkdir_with_parents(BiosDir().c_str(), 0755);
        g_file_set_contents(NotesPath().c_str(), (const char *)list->data,
                            list->len - 1, NULL);
    } else {
        notes = LocalNotes();
    }
    g_byte_array_free(list, TRUE);

    GByteArray *data = g_byte_array_new();
    code = http_get(Env("XBEMU_OXOS_API", releases_api), data, NULL, &err);
    if (err) {
        error_free(err);
        err = NULL;
    }
    bool ok = code == 200;
    if (ok) {
        try {
            json j = json::parse(data->data, data->data + data->len);
            for (auto &r : j) {
                if (r.value("draft", false) || r.value("prerelease", false)) {
                    continue;
                }
                std::string tag = r.value("tag_name", "");
                if (!IsTag(tag)) {
                    continue;
                }
                std::string want = "oxos_" + tag + ".zip";
                for (auto &a : r["assets"]) {
                    std::string digest = a.value("digest", "");
                    if (a.value("name", "") == want &&
                        digest.compare(0, 7, "sha256:") == 0) {
                        releases.push_back({ tag,
                            a.value("browser_download_url", ""),
                            digest.substr(7) });
                        break;
                    }
                }
            }
        } catch (...) {
            ok = false;
        }
    }
    g_byte_array_free(data, TRUE);
    std::sort(releases.begin(), releases.end(),
              [](const Release &a, const Release &b) {
                  return Newer(a.tag, b.tag);
              });
    fprintf(stderr, "oxos: check %s, %d releases, %d notes\n",
            ok ? "done" : "failed", (int)releases.size(), (int)notes.size());

    // Taken over by the main thread in AfterCheck
    self->m_found = releases;
    self->m_found_notes = notes;
    self->m_error = ok ? "" : "GitHub could not be reached.";
    self->m_status = ok ? CHECKED : FAILED;
    return NULL;
}

// The newest release that is not marked broken
bool OxosWindow::Newest(Release *out)
{
    for (auto &r : m_releases) {
        if (!IsBroken(m_notes, r.tag)) {
            *out = r;
            return true;
        }
    }
    return false;
}

void OxosWindow::AfterCheck()
{
    if (m_status == CHECKED) {
        m_releases = m_found;
        m_notes = m_found_notes;
    }
    Keys c = Choice();
    c["checked"] = std::to_string((long long)(g_get_real_time() / 1000000));
    SaveChoice(c);

    bool asked = m_asked;
    m_asked = false;
    if (m_status == FAILED) {
        if (asked) {
            xemu_queue_error_message("Could not look for a new OXOS: "
                                     "GitHub could not be reached.");
        }
        return;
    }
    std::string cur = CurrentTag();
    Release r;
    if (!Newest(&r) || cur.empty() || !Newer(r.tag, cur)) {
        if (asked) {
            std::string msg = "OXOS " + (cur.empty() ? r.tag : cur) +
                              " is the newest.";
            xemu_queue_notification(msg.c_str());
        }
        return;
    }
    if (!asked && c["skip"] == r.tag) {
        fprintf(stderr, "oxos: %s skipped by the player\n", r.tag.c_str());
        return;
    }
    if (m_prompt == NONE) {
        m_offer = r;
        m_prompt = OFFER;
    }
}

void OxosWindow::StartDownload(const Release &r)
{
    m_offer = r;
    m_progress = 0;
    m_error.clear();
    m_status = DOWNLOADING;
    m_prompt = PROGRESS;
    g_thread_unref(g_thread_new("oxos-download", DownloadThread, this));
}

gpointer OxosWindow::DownloadThread(gpointer opaque)
{
    OxosWindow *self = (OxosWindow *)opaque;
    Release r = self->m_offer;
    std::string error;

    http_progress_cb_info info = {};
    info.userptr = self;
    info.progress = [](http_progress_cb_info *i) -> int {
        OxosWindow *w = (OxosWindow *)i->userptr;
        if (i->dltotal) {
            w->m_progress = (int)(i->dlnow * 100 / i->dltotal);
        }
        return 0;
    };
    GByteArray *data = g_byte_array_new();
    Error *err = NULL;
    int code = http_get(r.zip_url.c_str(), data, &info, &err);
    if (err) {
        error_free(err);
    }

    void *bin = NULL;
    size_t bin_len = 0;
    if (code != 200) {
        error = "The download did not work. Check the internet connection "
                "and try again.";
    } else if (g_ascii_strcasecmp(Sha256(data->data, data->len).c_str(),
                                  r.digest.c_str()) != 0) {
        error = "The download was damaged (its SHA-256 does not match the "
                "release). Nothing was changed.";
    } else {
        mz_zip_archive zip = {};
        std::string want = "bios_" + r.tag + ".bin";
        if (mz_zip_reader_init_mem(&zip, data->data, data->len, 0)) {
            int i = mz_zip_reader_locate_file(&zip, want.c_str(), NULL, 0);
            if (i >= 0) {
                bin = mz_zip_reader_extract_to_heap(&zip, i, &bin_len, 0);
            }
            mz_zip_reader_end(&zip);
        }
        // The full size image (1 MB); a 256 KB one also boots
        if (!bin || (bin_len != 1024 * 1024 && bin_len != 256 * 1024)) {
            error = "The release has no BIOS file XBemu can use (" + want +
                    "). Nothing was changed.";
        }
    }
    g_byte_array_free(data, TRUE);

    if (error.empty()) {
        g_mkdir_with_parents(BiosDir().c_str(), 0755);
        std::string target = BiosFile(r.tag);
        std::string part = target + ".part";
        GError *gerr = NULL;
        if (!g_file_set_contents(part.c_str(), (const gchar *)bin, bin_len,
                                 &gerr)) {
            error = std::string("It could not be saved: ") + gerr->message;
            g_error_free(gerr);
        } else {
            g_remove(target.c_str());
            if (g_rename(part.c_str(), target.c_str()) != 0) {
                error = "It could not be saved in " + BiosDir() + ".";
                g_remove(part.c_str());
            }
        }
    }
    if (bin) {
        mz_free(bin);
    }
    fprintf(stderr, "oxos: download of %s %s\n", r.tag.c_str(),
            error.empty() ? "done" : error.c_str());
    self->m_error = error;
    self->m_downloaded = error.empty() ? r.tag : "";
    self->m_status = error.empty() ? DOWNLOADED : FAILED;
    return NULL;
}

static bool BeginPrompt(const char *title, float width)
{
    float scale = g_viewport_mgr.m_scale;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width * scale, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin(title, NULL,
                      ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return false;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (width - 20) * scale);
    return true;
}

static void EndPrompt()
{
    ImGui::PopTextWrapPos();
    ImGui::End();
}

// Tests answer the prompts without a mouse: XBEMU_OXOS_TEST_ANSWER lists
// buttons by their first word, lower case ("update,later"), pressed in
// order once a prompt has shown for 4 seconds
static double prompt_since = 0;
static guint answers_used = 0;

static bool PromptButton(const char *label, const char *key = NULL)
{
    if (ImGui::Button(label)) {
        return true;
    }
    const char *env = getenv("XBEMU_OXOS_TEST_ANSWER");
    if (!env || ImGui::GetTime() - prompt_since < 4.0) {
        return false;
    }
    gchar **list = g_strsplit(env, ",", -1);
    bool hit = false;
    if (answers_used < g_strv_length(list)) {
        std::string name = key ? key : label;
        name = name.substr(0, name.find(' '));
        for (auto &c : name) {
            c = g_ascii_tolower(c);
        }
        if (name == list[answers_used]) {
            hit = true;
            answers_used++;
            prompt_since = ImGui::GetTime();
            fprintf(stderr, "oxos: test pressed %s\n", label);
        }
    }
    g_strfreev(list);
    return hit;
}

static void Heading(const std::string &text)
{
    ImGui::PushFont(g_font_mgr.m_menu_font_medium);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopFont();
    ImGui::Spacing();
}

static void GameRunningWarning()
{
    if (runstate_is_running() && g_config.sys.files.dvd_path &&
        g_config.sys.files.dvd_path[0]) {
        ImGui::Spacing();
        ImGui::TextColored(bad_color, "A game is running: anything not saved "
                                      "in it is lost when XBemu restarts.");
    }
}

void OxosWindow::DrawOffer()
{
    if (!BeginPrompt("OXOS BIOS update", 480)) {
        return;
    }
    std::string tag = m_offer.tag, cur = CurrentTag();
    Heading("OXOS " + tag + " is out");
    ImGui::Text("You have OXOS %s.", cur.c_str());
    ImGui::Spacing();
    if (IsTested(m_notes, tag)) {
        ImGui::TextColored(good_color, "Tested with XBemu.");
    } else {
        ImGui::TextColored(warn_color, "Not tested with XBemu yet.");
        ImGui::TextWrapped("Some games may not start or may freeze with it. "
                           "You can go back to OXOS %s any time in "
                           "Machine > BIOS.", cur.c_str());
    }
    ImGui::Spacing();
    ImGui::TextWrapped("XBemu downloads it from github.com/kooscode/oxos, "
                       "checks the file and restarts to use it. Your "
                       "console, Xbox Live sign-in, saves and games are "
                       "kept.");
    GameRunningWarning();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (PromptButton("Update")) {
        StartDownload(m_offer);
    }
    ImGui::SameLine();
    if (PromptButton("Not now")) {
        m_prompt = NONE;
    }
    ImGui::SameLine();
    if (PromptButton("Skip this version")) {
        Keys c = Choice();
        c["skip"] = tag;
        SaveChoice(c);
        m_prompt = NONE;
    }
    EndPrompt();
}

void OxosWindow::DrawProgress()
{
    if (!BeginPrompt("OXOS BIOS update", 420)) {
        return;
    }
    int s = m_status;
    if (s == DOWNLOADING) {
        Heading("Downloading OXOS " + m_offer.tag);
        ImGui::ProgressBar(m_progress / 100.0f, ImVec2(-1, 0));
    } else if (s == DOWNLOADED) {
        UseTag(m_downloaded, true);
        m_prompt = RESTART;
    } else {
        Heading("OXOS " + m_offer.tag + " was not installed");
        ImGui::TextColored(bad_color, "%s", m_error.c_str());
        ImGui::TextWrapped("The console keeps OXOS %s.", CurrentTag().c_str());
        ImGui::Spacing();
        if (PromptButton("Try again")) {
            StartDownload(m_offer);
        }
        ImGui::SameLine();
        if (PromptButton("Close")) {
            m_prompt = NONE;
            m_status = IDLE;
        }
    }
    EndPrompt();
}

void OxosWindow::DrawRestart()
{
    if (!BeginPrompt("OXOS BIOS update", 440)) {
        return;
    }
    std::string tag = CurrentTag();
    Heading("OXOS " + tag + " is ready");
    ImGui::TextWrapped("Restart XBemu to start the console with it.");
    GameRunningWarning();
    ImGui::Spacing();
    if (PromptButton("Restart now")) {
        m_prompt = NONE;
        m_status = IDLE;
        ActionRestartApp();
    }
    ImGui::SameLine();
    if (PromptButton("Later")) {
        m_prompt = NONE;
        m_status = IDLE;
        std::string msg = "OXOS " + tag + " starts the next time you open "
                          "XBemu.";
        xemu_queue_notification(msg.c_str());
    }
    EndPrompt();
}

void OxosWindow::DrawSwitch()
{
    if (!BeginPrompt("Change the BIOS", 440)) {
        return;
    }
    std::string tag = m_switch_to, cur = CurrentTag();
    Heading("Switch to OXOS " + tag + "?");
    ImGui::Text("The console runs OXOS %s now.", cur.c_str());
    ImGui::Spacing();
    std::string why;
    if (IsBroken(m_notes, tag, &why)) {
        ImGui::TextColored(bad_color, "Marked broken with XBemu%s%s",
                           why.empty() ? "." : ": ", why.c_str());
    } else if (IsTested(m_notes, tag)) {
        ImGui::TextColored(good_color, "Tested with XBemu.");
    } else {
        ImGui::TextColored(warn_color, "Not tested with XBemu yet.");
    }
    ImGui::TextWrapped("XBemu restarts to use it. Your console, Xbox Live "
                       "sign-in, saves and games are kept.");
    GameRunningWarning();
    ImGui::Spacing();
    if (PromptButton("Switch and restart")) {
        m_prompt = NONE;
        UseTag(tag, Newer(tag, cur));
        ActionRestartApp();
    }
    ImGui::SameLine();
    if (PromptButton("Cancel")) {
        m_prompt = NONE;
    }
    EndPrompt();
}

void OxosWindow::DrawRollback()
{
    Keys c = Choice();
    std::string tag = c["pending"], prev = c["previous"];
    if (!BeginPrompt("OXOS BIOS", 460)) {
        return;
    }
    Heading("Did OXOS " + tag + " work?");
    ImGui::TextWrapped("You switched to OXOS %s and XBemu was closed soon "
                       "after. If a game froze or did not start, go back to "
                       "OXOS %s.", tag.c_str(), prev.c_str());
    ImGui::Spacing();
    std::string back = "Go back to " + prev, keep = "Keep " + tag;
    if (PromptButton(back.c_str(), "back")) {
        m_prompt = NONE;
        UseTag(prev, false);
        ActionRestartApp();
    }
    ImGui::SameLine();
    if (PromptButton(keep.c_str(), "keep")) {
        m_prompt = NONE;
        c.erase("pending");
        c.erase("previous");
        c.erase("starts");
        SaveChoice(c);
    }
    EndPrompt();
}

void OxosWindow::Draw()
{
    double now = ImGui::GetTime();
    if (m_start_time < 0) {
        m_start_time = now;
    }
    // At the start (after the first frames): a new BIOS that did not get
    // far last time, and the daily look for a new one
    if (!m_started && now - m_start_time > 3.0) {
        m_started = true;
        m_notes = LocalNotes();
        Keys c = Choice();
        std::string cur = CurrentTag();
        if (!c["pending"].empty()) {
            if (c["pending"] != cur ||
                !g_file_test(BiosFile(c["previous"]).c_str(),
                             G_FILE_TEST_EXISTS)) {
                c.erase("pending");
                c.erase("previous");
                c.erase("starts");
            } else {
                int starts = atoi(c["starts"].c_str());
                if (starts >= 1) {
                    m_prompt = ROLLBACK;
                }
                c["starts"] = std::to_string(starts + 1);
            }
            SaveChoice(c);
        }
        double checked = g_ascii_strtod(c["checked"].c_str(), NULL);
        if (!cur.empty() && (getenv("XBEMU_OXOS_CHECK") ||
            g_get_real_time() / 1e6 - checked > check_every)) {
            StartCheck(false);
        }
    }

    // A new BIOS that ran a while worked
    if (runstate_is_running()) {
        m_ran += ImGui::GetIO().DeltaTime;
        if (m_ran >= boot_ok_after && m_ran - ImGui::GetIO().DeltaTime <
                                          boot_ok_after) {
            Keys c = Choice();
            if (!c["pending"].empty()) {
                fprintf(stderr, "oxos: %s ran, kept\n", c["pending"].c_str());
                c.erase("pending");
                c.erase("previous");
                c.erase("starts");
                SaveChoice(c);
                if (m_prompt == ROLLBACK) {
                    m_prompt = NONE;
                }
            }
        }
    }

    // A check that finished (a failed download waits in its window)
    int s = m_status;
    if (s == CHECKED || (s == FAILED && m_prompt != PROGRESS)) {
        AfterCheck();
        m_status = IDLE;
    }

    static Prompt last = NONE;
    if (m_prompt != last) {
        last = m_prompt;
        prompt_since = now;
    }
    switch (m_prompt) {
    case OFFER: DrawOffer(); break;
    case PROGRESS: DrawProgress(); break;
    case RESTART: DrawRestart(); break;
    case SWITCH: DrawSwitch(); break;
    case ROLLBACK: DrawRollback(); break;
    default: break;
    }
}

void OxosWindow::Menu()
{
    std::vector<std::string> tags = LocalTags();
    std::string cur = CurrentTag();
    if (tags.empty()) {
        ImGui::MenuItem("OXOS", NULL, true, false);
    }
    for (auto &tag : tags) {
        std::string label = "OXOS " + tag;
        const char *mark = IsBroken(m_notes, tag) ? "broken" :
                           IsTested(m_notes, tag) ? "tested" : "";
        if (ImGui::MenuItem(label.c_str(), mark, tag == cur) && tag != cur) {
            m_switch_to = tag;
            m_prompt = SWITCH;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(tag == cur ? "The BIOS the console runs now." :
                              "Switch the console to this version (XBemu "
                              "restarts).");
        }
    }
    ImGui::Separator();
    Release r;
    int s = m_status;
    bool busy = s == CHECKING || s == DOWNLOADING;
    if (!m_releases.empty() && Newest(&r) && !cur.empty() &&
        Newer(r.tag, cur) &&
        !g_file_test(BiosFile(r.tag).c_str(), G_FILE_TEST_EXISTS)) {
        std::string label = "Update to OXOS " + r.tag + "...";
        if (ImGui::MenuItem(label.c_str(), NULL, false, !busy)) {
            m_offer = r;
            m_prompt = OFFER;
        }
    }
    if (ImGui::MenuItem(s == CHECKING ? "Looking for a new OXOS..." :
                                        "Check for a new OXOS",
                        NULL, false, !busy)) {
        CheckNow();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                             ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("OXOS is the open-source Xbox BIOS "
                          "(github.com/kooscode/oxos). XBemu looks for a new "
                          "version once a day and asks before updating.");
    }
}
