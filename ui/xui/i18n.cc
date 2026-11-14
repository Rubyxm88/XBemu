//
// xemu User Interface: XBemu in the console's language (see i18n.hh)
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
#include "i18n.hh"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glib.h>
#include <SDL3/SDL.h>

#include "common.hh"
#include "font-manager.hh"
#include "viewport-manager.hh"
#include "versions.hh"
#include "../xbemu-brand.h"
#include "data/xbemu-lang.tsv.h"

extern "C" {
#include "hw/xbox/xbox-language.h"
#include "ui/xemu-i18n.h"
#include "ui/xemu-settings.h"
}

const char *const kI18nReportItem = "Report a translation problem...";

// --- Languages: the table's column names and the console's numbers

struct Lang {
    const char *code;
    const char *name; // in the language itself
    uint32_t xbox;
};

static const Lang kLangs[] = {
    { "en", "English", XBOX_LANGUAGE_ENGLISH },
    { "de", "Deutsch", XBOX_LANGUAGE_GERMAN },
    { "fr", "Français", XBOX_LANGUAGE_FRENCH },
    { "es", "Español", XBOX_LANGUAGE_SPANISH },
    { "it", "Italiano", XBOX_LANGUAGE_ITALIAN },
    { "pt", "Português", XBOX_LANGUAGE_PORTUGUESE },
};
static const int kLangCount = sizeof(kLangs) / sizeof(kLangs[0]);

// --- The table

struct Table {
    std::vector<std::string> english, text;
    std::unordered_map<uint64_t, uint32_t> index; // hash of english -> row
};

static Table s_tables[kLangCount];
static int s_cur;         // index into kLangs, 0 = English
// For other threads: the tables are loaded (never change after) and the
// language they should use
static int s_ready, s_cur_shared;
static bool s_log_missing;
static std::unordered_set<uint64_t> s_missing_logged;
// Icon + translated text, built once per text (values never move)
static std::unordered_map<std::string, std::string> s_composed;

static uint64_t Hash(const char *s, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ull;
    }
    return h;
}

// A cell: \n, \t and \\ written out
static std::string Unescape(const char *s, size_t n)
{
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[++i];
            out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else {
            out += s[i];
        }
    }
    return out;
}

static void LoadTable()
{
    const char *p = (const char *)xbemu_lang_data;
    const char *end = p + xbemu_lang_size;
    int column_lang[16];
    int columns = 0;
    bool header = true;
    int rows = 0;

    while (p < end) {
        const char *eol = (const char *)memchr(p, '\n', end - p);
        if (!eol) {
            eol = end;
        }
        const char *line_end = eol;
        if (line_end > p && line_end[-1] == '\r') {
            line_end--;
        }
        if (line_end > p && *p != '#') {
            // Split on tabs
            std::vector<std::string> cells;
            const char *c = p;
            while (true) {
                const char *tab = (const char *)memchr(c, '\t', line_end - c);
                const char *cell_end = tab ? tab : line_end;
                cells.push_back(Unescape(c, cell_end - c));
                if (!tab) {
                    break;
                }
                c = tab + 1;
            }
            if (header) {
                header = false;
                columns = 0;
                for (size_t i = 0; i < cells.size() && i < 16; i++) {
                    column_lang[i] = -1;
                    for (int l = 0; l < kLangCount; l++) {
                        if (cells[i] == kLangs[l].code) {
                            column_lang[i] = l;
                        }
                    }
                    columns = i + 1;
                }
            } else if (!cells.empty() && !cells[0].empty()) {
                rows++;
                for (size_t i = 1; i < cells.size() && (int)i < columns; i++) {
                    int l = column_lang[i];
                    if (l <= 0 || cells[i].empty()) {
                        continue;
                    }
                    Table &t = s_tables[l];
                    uint64_t h = Hash(cells[0].data(), cells[0].size());
                    if (t.index.count(h)) {
                        continue; // the first row wins
                    }
                    t.index[h] = t.english.size();
                    t.english.push_back(cells[0]);
                    t.text.push_back(cells[i]);
                }
            }
        }
        p = eol + 1;
    }

    std::string counts;
    for (int l = 1; l < kLangCount; l++) {
        counts += std::string(" ") + kLangs[l].code + " " +
                  std::to_string(s_tables[l].english.size());
    }
    fprintf(stderr, "language: table has %d texts;%s\n", rows,
            counts.c_str());
}

// The translation of exactly [s, s + n), or NULL
static const std::string *Find(const char *s, size_t n)
{
    const Table &t = s_tables[s_cur];
    auto it = t.index.find(Hash(s, n));
    if (it == t.index.end()) {
        return NULL;
    }
    const std::string &en = t.english[it->second];
    if (en.size() != n || memcmp(en.data(), s, n)) {
        return NULL;
    }
    return &t.text[it->second];
}

// From any thread (see ui/xemu-i18n.h): whole texts only, nothing logged
const char *xemu_i18n_tr(const char *english)
{
    if (!__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE) || !english) {
        return english;
    }
    int l = __atomic_load_n(&s_cur_shared, __ATOMIC_RELAXED);
    if (l <= 0 || l >= kLangCount) {
        return english;
    }
    const Table &t = s_tables[l];
    size_t n = strlen(english);
    auto it = t.index.find(Hash(english, n));
    if (it == t.index.end() || t.english[it->second] != english) {
        return english;
    }
    return t.text[it->second].c_str();
}

// Icons (Font Awesome, in the private use area U+E000-U+F8FF) and spaces in
// front of a text: how many bytes
static size_t IconPrefix(const char *s, size_t n)
{
    size_t p = 0;
    while (p < n) {
        unsigned char c = s[p];
        if (c == ' ') {
            p++;
        } else if (p + 2 < n &&
                   (c == 0xEE ||
                    (c == 0xEF && (unsigned char)s[p + 1] <= 0xA3))) {
            p += 3;
        } else {
            break;
        }
    }
    return p;
}

static bool HasLetter(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (g_ascii_isalpha(s[i])) {
            return true;
        }
    }
    return false;
}

// For the missing-text log: the hook also sees what was already translated
// (and texts printed from a translated format), and texts that are never
// translated. True for those.
static bool IsOutput(const char *s, size_t n)
{
    static int built = -1;
    static std::unordered_set<uint64_t> whole;
    static std::vector<std::string> starts; // translated formats up to the %
    if (built != s_cur) {
        built = s_cur;
        whole.clear();
        starts.clear();
        const Table &t = s_tables[s_cur];
        for (const std::string &x : t.text) {
            whole.insert(Hash(x.data(), x.size()));
            size_t pct = x.find('%');
            if (pct != std::string::npos && pct >= 4) {
                starts.push_back(x.substr(0, pct));
            }
        }
        for (const Lang &l : kLangs) {
            whole.insert(Hash(l.name, strlen(l.name)));
        }
        // ImGui's own fallback window, never seen
        whole.insert(Hash("Debug", 5));
    }
    if (whole.count(Hash(s, n))) {
        return true;
    }
    for (const std::string &x : starts) {
        if (n >= x.size() && !memcmp(s, x.data(), x.size())) {
            return true;
        }
    }
    return false;
}

// The translation of [s, s + n) or NULL; the result stays valid
static const char *Translate(const char *s, size_t n, size_t *out_n)
{
    if (s_cur == 0 || n == 0 || n > 8000) {
        return NULL;
    }
    const std::string *r = Find(s, n);
    if (r) {
        *out_n = r->size();
        return r->c_str();
    }
    // Icons or spaces in front, spaces after (tagged menu items pad their
    // label): translate the middle and put them back
    size_t p = IconPrefix(s, n);
    size_t q = n;
    while (q > p && s[q - 1] == ' ') {
        q--;
    }
    if ((p > 0 || q < n) && p < q) {
        r = Find(s + p, q - p);
        if (r) {
            std::string key(s, n);
            auto it = s_composed.find(key);
            if (it == s_composed.end()) {
                it = s_composed
                         .emplace(key, std::string(s, p) + *r +
                                           std::string(s + q, n - q))
                         .first;
            }
            *out_n = it->second.size();
            return it->second.c_str();
        }
    }
    if (s_log_missing && n >= 3 && HasLetter(s, n) && !IsOutput(s, n)) {
        uint64_t h = Hash(s, n);
        if (s_missing_logged.insert(h).second) {
            std::string text(s, n);
            for (auto &c : text) {
                if (c == '\n') {
                    c = '|';
                }
            }
            fprintf(stderr, "language missing (%s): %s\n",
                    kLangs[s_cur].code, text.c_str());
        }
    }
    return NULL;
}

static const char *TranslateHook(const char *text, const char *text_end,
                                 const char **out_end)
{
    size_t n = text_end ? (size_t)(text_end - text) : strlen(text);
    size_t out_n = 0;
    const char *r = Translate(text, n, &out_n);
    if (r) {
        *out_end = r + out_n;
    }
    return r;
}

const char *Tr(const char *english)
{
    size_t n = 0;
    const char *r = Translate(english, strlen(english), &n);
    return r ? r : english;
}

std::string TrS(const std::string &english)
{
    size_t n = 0;
    const char *r = Translate(english.data(), english.size(), &n);
    return r ? std::string(r, n) : english;
}

std::string TrF(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), xemu_i18n_tr(fmt), ap);
    va_end(ap);
    return buf;
}

const char *I18nCode()
{
    return kLangs[s_cur].code;
}

const char *I18nName()
{
    return kLangs[s_cur].name;
}

bool I18nIsEnglish()
{
    return s_cur == 0;
}

// --- The notice, once per language

static bool s_notice_open;
static bool s_report_open;
static double s_flash_until;
static bool s_help_open_pending;
static int s_help_open_frame = -10;

static std::string NoticeFile()
{
    return std::string(xemu_settings_get_base_path()) +
           "language-notice.txt";
}

static bool Noticed(const char *code)
{
    gchar *text = NULL;
    bool found = false;
    if (g_file_get_contents(NoticeFile().c_str(), &text, NULL, NULL)) {
        gchar **words = g_strsplit_set(text, " \r\n", -1);
        for (int i = 0; words[i]; i++) {
            if (!strcmp(words[i], code)) {
                found = true;
            }
        }
        g_strfreev(words);
        g_free(text);
    }
    return found;
}

static void MarkNoticed(const char *code)
{
    if (Noticed(code)) {
        return;
    }
    gchar *text = NULL;
    std::string all;
    if (g_file_get_contents(NoticeFile().c_str(), &text, NULL, NULL)) {
        all = text;
        g_free(text);
    }
    all += std::string(code) + "\n";
    g_file_set_contents(NoticeFile().c_str(), all.c_str(), -1, NULL);
}

void I18nInit()
{
    s_log_missing = getenv("XEMU_I18N_MISSING") != NULL;
    LoadTable();
    __atomic_store_n(&s_ready, 1, __ATOMIC_RELEASE);
}

void I18nUpdate()
{
    static bool loaded;
    if (!loaded) {
        loaded = true;
        I18nInit();
    }
    int want = s_cur;
    int fixed = g_config.display.ui.language; // 0 = auto, then kLangs + 1
    const char *env = getenv("XEMU_LANGUAGE");
    bool chosen = true; // picked by hand, not taken from the console
    if (env && *env) {
        want = 0;
        for (int l = 0; l < kLangCount; l++) {
            if (!strcmp(env, kLangs[l].code)) {
                want = l;
            }
        }
    } else if (fixed > 0 && fixed <= kLangCount) {
        want = fixed - 1;
    } else {
        chosen = false;
        uint32_t lang;
        if (xbox_language_get(&lang)) {
            want = 0; // Japanese, Korean and Chinese: English for now
            for (int l = 0; l < kLangCount; l++) {
                if (kLangs[l].xbox == lang) {
                    want = l;
                }
            }
        }
    }
    if (want == s_cur) {
        return;
    }
    s_cur = want;
    __atomic_store_n(&s_cur_shared, want, __ATOMIC_RELAXED);
    s_composed.clear();
    GImGuiTranslate = s_cur ? TranslateHook : NULL;
    fprintf(stderr, "language: %s\n", kLangs[s_cur].code);
    // The notice tells about the automatic switch; a language picked by
    // hand needs no telling
    s_notice_open =
        s_cur != 0 && !chosen && !Noticed(kLangs[s_cur].code);
    if (s_cur == 0) {
        s_report_open = false;
    }
}

// Help > Language: Automatic (the console's) or one fixed language. The
// names stay in their own language so each can be found in any language.
void I18nDrawLanguageMenu()
{
    int &cfg = g_config.display.ui.language;
    if (ImGui::MenuItem("Automatic (console language)", NULL, cfg == 0)) {
        cfg = 0;
    }
    ImGui::Separator();
    for (int l = 0; l < kLangCount; l++) {
        // Drawn untranslated: the hook would leave these alone anyway
        if (ImGui::MenuItem(kLangs[l].name, NULL, cfg == l + 1)) {
            cfg = l + 1;
        }
    }
}

void I18nOpenReport()
{
    s_report_open = true;
}

bool I18nWantsHelpMenuOpen()
{
    bool r = s_help_open_pending;
    s_help_open_pending = false;
    return r;
}

bool I18nFlashReportItem()
{
    s_help_open_frame = ImGui::GetFrameCount();
    return ImGui::GetTime() < s_flash_until;
}

bool I18nKeepMenuBarAwake()
{
    return ImGui::GetTime() < s_flash_until ||
           ImGui::GetFrameCount() - s_help_open_frame <= 2;
}

static bool BeginBox(const char *title, float width, bool *open)
{
    float scale = g_viewport_mgr.m_scale;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width * scale, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin(title, open,
                      ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return false;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (width - 20) * scale);
    return true;
}

static void EndBox()
{
    ImGui::PopTextWrapPos();
    ImGui::End();
}

static std::string ReportPath()
{
    return std::string(Tr("Help")) + "  >  " + Tr(kI18nReportItem);
}

static void DrawNotice()
{
    if (!BeginBox("Language###xbemu-language-notice", 470, NULL)) {
        return;
    }
    ImGui::PushFont(g_font_mgr.m_menu_font_medium);
    ImGui::TextUnformatted(I18nName());
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::TextWrapped("XBemu now uses your console's language. The XBL "
                       "Dashboard sets it in its settings.");
    ImGui::Spacing();
    ImGui::TextWrapped("To keep one language no matter what the console "
                       "uses, pick it here:");
    ImGui::TextColored(ImVec4(0.36f, 0.82f, 0.38f, 1.0f), "%s",
                       (std::string(Tr("Help")) + "  >  " +
                        Tr("Language"))
                           .c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("This translation is new and may have mistakes. If "
                       "something reads wrong or is cut off, please tell "
                       "us here:");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.36f, 0.82f, 0.38f, 1.0f), "%s",
                       ReportPath().c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    bool close = false;
    if (ImGui::Button("Show me")) {
        close = true;
        if (g_config.display.ui.show_menubar) {
            s_help_open_pending = true;
            s_flash_until = ImGui::GetTime() + 6.0;
        } else {
            s_report_open = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("OK")) {
        close = true;
    }
    if (close) {
        s_notice_open = false;
        MarkNoticed(kLangs[s_cur].code);
    }
    EndBox();
}

// --- The report

static char s_details[1500];
static bool s_report_sent;

static std::string UrlEncode(const std::string &s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (g_ascii_isalnum(c) || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

static const char *SystemName()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "Mac";
#else
    return "Linux";
#endif
}

// What is sent: always in English words, so it can be read by anyone
static std::string ReportText()
{
    return std::string("Language: ") + kLangs[s_cur].name + " (" +
           kLangs[s_cur].code + ")\nBuild: " + InstalledBuildText() +
           "\nSystem: " + SystemName() + "\n\n" + s_details;
}

static std::string ReportUrl()
{
    std::string details = s_details;
    // The page's address has a length limit; keep it well under
    while (true) {
        std::string title = std::string("Translation (") +
                            kLangs[s_cur].name + "): " +
                            details.substr(0, 60);
        size_t nl = title.find('\n');
        if (nl != std::string::npos) {
            title.resize(nl);
        }
        std::string url = std::string("https://github.com/") + XBEMU_REPO +
                          "/issues/new?template=4-translation.yml&title=" +
                          UrlEncode(title) + "&language=" +
                          UrlEncode(std::string(kLangs[s_cur].name) + " (" +
                                    kLangs[s_cur].code + ")") +
                          "&build=" + UrlEncode(InstalledBuildText()) +
                          "&system=" + UrlEncode(SystemName()) +
                          "&details=" + UrlEncode(details);
        if (url.size() < 7000 || details.size() < 100) {
            return url;
        }
        details.resize(details.size() * 3 / 4);
    }
}

static void DrawReport()
{
    float scale = g_viewport_mgr.m_scale;
    if (!BeginBox("Report a translation problem###xbemu-language-report", 520,
                  &s_report_open)) {
        return;
    }
    ImGui::TextWrapped("Tell us what reads wrong and where you saw it, for "
                       "example the menu and the text. A better wording is "
                       "welcome too.");
    ImGui::Spacing();
    ImGui::Text("Language: %s", I18nName());
    ImGui::Text("Build: %s", InstalledBuildText().c_str());
    ImGui::Spacing();
    ImGui::InputTextMultiline("##xbemu-language-details", s_details,
                              sizeof(s_details),
                              ImVec2(500 * scale,
                                     ImGui::GetTextLineHeight() * 6));
    ImGui::Spacing();
    ImGui::TextWrapped("Open on GitHub fills in a new report on XBemu's "
                       "GitHub page; you need a GitHub account to send it. "
                       "No account? Copy the report and paste it in the "
                       "XBemu Discord.");
    if (s_report_sent) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.36f, 0.82f, 0.38f, 1.0f),
                           "Opened in your browser. Press Submit there to "
                           "send it.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    bool empty = s_details[0] == 0;
    ImGui::BeginDisabled(empty);
    if (ImGui::Button("Open on GitHub")) {
        std::string url = ReportUrl();
        fprintf(stderr, "language: report opened (%zu characters)\n",
                url.size());
        SDL_OpenURL(url.c_str());
        s_report_sent = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy report")) {
        SDL_SetClipboardText(ReportText().c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        s_report_open = false;
    }
    EndBox();
    if (!s_report_open) {
        s_report_sent = false;
    }
}

void I18nDraw()
{
    if (s_notice_open) {
        DrawNotice();
    }
    if (s_report_open) {
        DrawReport();
    }
}
