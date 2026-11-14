//
// xemu User Interface: show a newly loaded disc on the XBL Dashboard
//
// The XBL Dashboard reads the disc only when the player moves in its menu,
// and OXOS keeps the old disc mounted until a read fails. So after a disc
// change the menu needs moves before the Disc tile shows the new disc. Here
// xemu makes them itself, each move followed by its way back so the cursor
// ends where the player left it, under a cover with the XBemu logo and
// "Loading disc...", with the console's sound off so the menu clicks are not
// heard. Then "Disc loaded", and the cover fades to the dashboard.
//
// xemu cannot ask where the cursor is, so it looks: a small copy of the
// console's picture before and after a move. A move the dashboard took
// changes the picture (the highlight moves); one at the end of a row does
// not, and the next pair is tried (right/left, left/right, down/up,
// up/down). The way back follows only a move that was taken. Pairs repeat
// until the dashboard has mounted the new disc (the drive's READ CAPACITY,
// counted in hw/ide/atapi.c).
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
#include <epoxy/gl.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <algorithm>
#include <string>
#include <vector>
#include <filesystem>
#include "common.hh"
#include "i18n.hh"
#include "disc-refresh.hh"
#include "font-manager.hh"
#include "gl-helpers.hh"
#include "viewport-manager.hh"
#include "../xemu-input.h"
#include "../xemu-running-title.h"
#include "../xemu-dashinstall.h"
#include "../xemu-gamecache.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Seconds
#define FADE_IN      0.35
#define FIRST_PRESS  0.80   // after the disc change
#define PRESS_HOLD   0.20
#define SETTLE       0.70   // after a press: the picture shows the move
#define MOUNT_WAIT   2.00   // after a way back: time to mount the disc
#define LOADED_TIME  1.00   // "Disc loaded" on screen
#define FADE_TIME    0.45
#define DUCK_RAMP    0.20   // the sound fades out before the first press
#define UNMUTE_AFTER 0.80   // sound back no sooner after the last press

#define MAX_ROUNDS 3        // move pairs taken before giving up

// A dashboard must have run this long (ms), so a restart's leftovers don't
// count
#define DASH_UP_MS 3000

// Thumbnail pixels that changed this much (0-255, any channel) count, and
// this many of them (per mille of the thumbnail) mean the cursor moved
#define DIFF_LEVEL    40
#define DIFF_PERMILLE 2

extern "C" uint32_t xemu_atapi_capacity_reads; // hw/ide/atapi.c
extern "C" float xemu_audio_duck;              // hw/xbox/mcpx/apu/monitor.c

// A move and its way back, in the order they are tried
static const uint16_t pairs[4][2] = {
    { CONTROLLER_BUTTON_DPAD_RIGHT, CONTROLLER_BUTTON_DPAD_LEFT },
    { CONTROLLER_BUTTON_DPAD_LEFT, CONTROLLER_BUTTON_DPAD_RIGHT },
    { CONTROLLER_BUTTON_DPAD_DOWN, CONTROLLER_BUTTON_DPAD_UP },
    { CONTROLLER_BUTTON_DPAD_UP, CONTROLLER_BUTTON_DPAD_DOWN },
};
static const char *const pair_names[4][2] = {
    { "right", "left" }, { "left", "right" }, { "down", "up" }, { "up", "down" },
};

enum RefreshPhase {
    PHASE_MOVE,     // next: a pair's first move
    PHASE_MOVED,    // first move pressed, looking whether it was taken
    PHASE_BACK,     // way back pressed
    PHASE_MOUNT,    // waiting for the dashboard to mount the disc
    PHASE_LOADED,   // "Disc loaded"
};

static bool s_active;
static double s_start;          // ImGui time; < 0 until the first frame
static std::string s_name;
static uint16_t s_pressed;
static RefreshPhase s_phase;
static double s_phase_at;       // seconds when the phase started
static int s_pair, s_rounds;
static uint32_t s_mounts;       // READ CAPACITY count at the start
static double s_loaded_at;
static double s_last_press;     // seconds of the last press
static float s_angle, s_speed;  // the drawn disc's turn

// The console's picture, small: a texture to scale into, and the pixels
static GLuint s_tex, s_fbo;
static int s_tw, s_th;
static std::vector<uint8_t> s_thumb, s_before;
static bool s_thumb_ok;

static void EnsureTarget(int w, int h)
{
    if (s_tex && s_tw == w && s_th == h) {
        return;
    }
    if (!s_tex) {
        glGenTextures(1, &s_tex);
        glGenFramebuffers(1, &s_fbo);
    }
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s_tex, 0);
    s_tw = w;
    s_th = h;
}

// Each frame while active, right after the console's picture is drawn (and
// before the cover): an eighth-size copy of it, read back
void DiscRefreshCapture(int width, int height)
{
    if (!s_active || width < 8 || height < 8) {
        return;
    }
    GLint draw_fb = 0, read_fb = 0, tex = 0, pack = 4;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fb);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    while (glGetError() != GL_NO_ERROR) {
    }

    int w = width / 8, h = height / 8;
    EnsureTarget(w, h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, draw_fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_fbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, w, h, GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo);
    s_thumb.resize((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, s_thumb.data());
    bool ok = glGetError() == GL_NO_ERROR;
    if (!ok && s_thumb_ok) {
        fprintf(stderr, "disc refresh: no copy of the picture\n");
    }
    s_thumb_ok = ok;

    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fb);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
    }
}

// Per mille of the thumbnail's pixels that changed since s_before; -1 when
// there is nothing to compare
static int Changed(void)
{
    if (!s_thumb_ok || s_before.empty() || s_before.size() != s_thumb.size()) {
        return -1;
    }
    size_t n = s_thumb.size() / 4, changed = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *a = &s_before[i * 4], *b = &s_thumb[i * 4];
        int d = std::max({ abs(a[0] - b[0]), abs(a[1] - b[1]),
                           abs(a[2] - b[2]) });
        changed += d >= DIFF_LEVEL;
    }
    return (int)(changed * 1000 / n);
}

static void Press(uint16_t button)
{
    s_pressed = button;
    s_last_press = ImGui::GetTime() - s_start;
    __atomic_store_n(&xemu_input_forced_buttons, button, __ATOMIC_RELAXED);
}

static void Release(void)
{
    if (s_pressed) {
        __atomic_store_n(&xemu_input_forced_buttons, (uint16_t)0,
                         __ATOMIC_RELAXED);
        s_pressed = 0;
    }
}

static void SetDuck(float value)
{
    __atomic_store(&xemu_audio_duck, &value, __ATOMIC_RELAXED);
}

static void Stop(const char *why)
{
    Release();
    SetDuck(1.0f);
    s_active = false;
    fprintf(stderr, "disc refresh: %s\n", why);
    /* What the console gets from the player just after (a report: moves
       not taken for a second after the cover went away) */
    xemu_input_trace_start(6.0);
}

static void SetPhase(RefreshPhase phase, double t)
{
    s_phase = phase;
    s_phase_at = t;
}

static void Loaded(double t, const char *why)
{
    fprintf(stderr, "disc refresh: %s\n", why);
    s_loaded_at = t;
    SetPhase(PHASE_LOADED, t);
}

static bool Mounted(void)
{
    return __atomic_load_n(&xemu_atapi_capacity_reads, __ATOMIC_RELAXED) !=
           s_mounts;
}

// The presses, one at a time, each held PRESS_HOLD s
static void Step(double t)
{
    if (s_pressed && t >= s_phase_at + PRESS_HOLD) {
        Release();
    }
    switch (s_phase) {
    case PHASE_MOVE:
        if (t < FIRST_PRESS) {
            return;
        }
        s_before = s_thumb;
        Press(pairs[s_pair][0]);
        fprintf(stderr, "disc refresh: %s\n", pair_names[s_pair][0]);
        SetPhase(PHASE_MOVED, t);
        return;
    case PHASE_MOVED: {
        if (t < s_phase_at + SETTLE) {
            return;
        }
        int changed = Changed();
        fprintf(stderr, "disc refresh: picture changed %d/1000\n", changed);
        if (changed >= DIFF_PERMILLE || changed < 0) {
            // Taken (or no way to tell: then as if taken)
            Press(pairs[s_pair][1]);
            fprintf(stderr, "disc refresh: %s (back)\n",
                    pair_names[s_pair][1]);
            SetPhase(PHASE_BACK, t);
        } else if (++s_pair < 4) {
            fprintf(stderr, "disc refresh: not taken\n");
            SetPhase(PHASE_MOVE, t);
        } else {
            Loaded(t, "no move taken");
        }
        return;
    }
    case PHASE_BACK:
        if (t >= s_phase_at + PRESS_HOLD) {
            SetPhase(PHASE_MOUNT, t);
        }
        return;
    case PHASE_MOUNT:
        if (Mounted()) {
            Loaded(t + 0.3, "mounted");
        } else if (t >= s_phase_at + MOUNT_WAIT) {
            if (++s_rounds < MAX_ROUNDS) {
                fprintf(stderr, "disc refresh: not mounted yet, again\n");
                SetPhase(PHASE_MOVE, t);
            } else {
                Loaded(t, "not mounted, gave up");
            }
        }
        return;
    case PHASE_LOADED:
        return;
    }
}

extern "C" void xemu_disc_refresh_start(const char *path)
{
    if (xemu_running_title_get(NULL) ||
        xemu_running_title_system_ms() < DASH_UP_MS ||
        xemu_dashinstall_active()) {
        return; // a game runs (it reads the disc itself), or no dashboard yet
    }

    char name[64] = "";
    uint32_t id;
    if (xemu_gamecache_disc_title(&id, name, sizeof(name)) && name[0]) {
        s_name = name;
    } else {
        s_name = std::filesystem::path(path).stem().string();
    }
    Release();
    s_active = true;
    s_start = -1;
    s_pair = 0;
    s_rounds = 0;
    s_loaded_at = -1;
    s_last_press = 0;
    s_speed = 0;
    s_before.clear();
    s_mounts = __atomic_load_n(&xemu_atapi_capacity_reads, __ATOMIC_RELAXED);
    SetPhase(PHASE_MOVE, 0);
    fprintf(stderr, "disc refresh: start (%s)\n", s_name.c_str());
}

// --- The cover

static ImU32 Col(int r, int g, int b, float a)
{
    return IM_COL32(r, g, b, (int)(255 * std::min(1.0f, std::max(0.0f, a))));
}

// Text centred on x, top at y; returns its height
static float CentredText(ImDrawList *dl, ImFont *font, float x, float y,
                         ImU32 col, const char *text)
{
    ImVec2 sz = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, text);
    dl->AddText(font, font->FontSize, ImVec2(x - sz.x / 2, y), col, text);
    return sz.y;
}

// A disc seen from the front: silver with a sheen that turns with it, the
// clear hub and its hole
static void DrawDisc(ImDrawList *dl, ImVec2 c, float r, float angle, float a)
{
    float scale = g_viewport_mgr.m_scale;
    dl->AddCircleFilled(ImVec2(c.x, c.y + 5 * scale), r * 1.02f,
                        Col(0, 0, 0, 0.45f * a), 64);
    const int n = 72;
    float r_in = r * 0.36f;
    // Edge smoothing off for the segments: it left thin seams between them
    ImDrawListFlags flags = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (int i = 0; i < n; i++) {
        float a0 = (float)(2 * M_PI * i / n);
        float a1 = (float)(2 * M_PI * (i + 1) / n);
        float m = (a0 + a1) / 2;
        // Two bright bands opposite each other, a faint rainbow over them
        float lit = 0.5f + 0.5f * cosf(2 * (m - angle));
        lit = lit * lit;
        float hue = fmodf(m / (float)(2 * M_PI) + angle * 0.05f + 1.0f, 1.0f);
        float rr, gg, bb;
        ImGui::ColorConvertHSVtoRGB(hue, 0.35f, 1.0f, rr, gg, bb);
        float base = 120 + 115 * lit;
        float tint = 0.22f * lit;
        int cr = (int)(base * (1 - tint) + 255 * rr * tint);
        int cg = (int)(base * (1 - tint) + 255 * gg * tint);
        int cb = (int)(base * (1 - tint) + 255 * bb * tint);
        ImVec2 q0(c.x + cosf(a0) * r_in, c.y + sinf(a0) * r_in);
        ImVec2 q1(c.x + cosf(a0) * r, c.y + sinf(a0) * r);
        ImVec2 q2(c.x + cosf(a1) * r, c.y + sinf(a1) * r);
        ImVec2 q3(c.x + cosf(a1) * r_in, c.y + sinf(a1) * r_in);
        dl->AddQuadFilled(q0, q1, q2, q3, Col(cr, cg, cb, a));
    }
    dl->Flags = flags;
    dl->AddCircle(c, r, Col(255, 255, 255, 0.35f * a), 96, 1.5f * scale);
    dl->AddCircle(c, r * 0.97f, Col(0, 0, 0, 0.18f * a), 96, 1.0f * scale);
    // The clear hub, the stacking ring, the hole
    dl->AddCircleFilled(c, r_in, Col(58, 66, 62, a), 48);
    dl->AddCircle(c, r_in, Col(255, 255, 255, 0.25f * a), 48, 1.0f * scale);
    dl->AddCircle(c, r * 0.24f, Col(255, 255, 255, 0.12f * a), 48,
                  2.0f * scale);
    dl->AddCircleFilled(c, r * 0.12f, Col(9, 12, 10, a), 32);
    dl->AddCircle(c, r * 0.12f, Col(255, 255, 255, 0.20f * a), 32,
                  1.0f * scale);
}

void DiscRefreshDraw()
{
    if (!s_active) {
        return;
    }
    double now = ImGui::GetTime();
    if (s_start < 0) {
        s_start = now;
    }
    double t = now - s_start;

    // A game started meanwhile (the player chose it): hands off
    if (xemu_running_title_get(NULL)) {
        Stop("a game started, stopped");
        return;
    }

    Step(t);

    // The console's sound off from just before the first press until
    // "Disc loaded", and never sooner than UNMUTE_AFTER s after the last
    // press (the dashboard makes its click a moment after the press)
    double unmute = s_loaded_at < 0 ? 1e9 :
                    std::max(s_loaded_at, s_last_press + UNMUTE_AFTER);
    float duck;
    if (t < unmute) {
        duck = (float)(1.0 - (t - (FIRST_PRESS - DUCK_RAMP)) / DUCK_RAMP);
    } else {
        duck = (float)((t - unmute) / DUCK_RAMP);
    }
    SetDuck(std::min(1.0f, std::max(0.0f, duck)));

    double fade_out = s_loaded_at < 0 ? 1e9 :
                      std::max(s_loaded_at + LOADED_TIME, unmute);
    if (t >= fade_out + FADE_TIME) {
        Stop("done");
        return;
    }
    float a = 1.0f;
    if (t < FADE_IN) {
        a = (float)(t / FADE_IN);
    } else if (t > fade_out) {
        a = (float)(1.0 - (t - fade_out) / FADE_TIME);
    }
    a = a * a * (3 - 2 * a); // smooth
    bool done = s_phase == PHASE_LOADED && t >= s_loaded_at;

    // The disc turns fast while loading and slows to a stop once loaded
    float dt = std::min(0.1f, ImGui::GetIO().DeltaTime);
    s_speed = done ? std::max(0.0f, s_speed - 9.0f * dt) :
                     std::min(7.0f, s_speed + 14.0f * dt);
    s_angle = fmodf(s_angle + s_speed * dt, (float)(2 * M_PI));

    float scale = g_viewport_mgr.m_scale;
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImVec2 p0 = vp->Pos, p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    float W = vp->Size.x, H = vp->Size.y, cx = p0.x + W / 2;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();

    // Background: dark, a little green at the bottom
    dl->AddRectFilledMultiColor(p0, p1, Col(12, 15, 13, a), Col(12, 15, 13, a),
                                Col(6, 20, 9, a), Col(6, 20, 9, a));

    // Sizes, then the whole block centred on the screen
    float logo_w = std::min(W * 0.36f, 320.0f * scale);
    float logo_h = (g_xbemu_logo_tex && g_xbemu_logo_w > 0) ?
                   logo_w * g_xbemu_logo_h / g_xbemu_logo_w : 0;
    float r = std::min(H * 0.09f, 70.0f * scale);
    ImFont *f_title = g_font_mgr.m_menu_font_medium;
    ImFont *f_name = g_font_mgr.m_menu_font;
    if (f_name->CalcTextSizeA(f_name->FontSize, FLT_MAX, 0, s_name.c_str()).x >
        W * 0.9f) {
        f_name = g_font_mgr.m_menu_font_medium;
    }
    ImFont *f_small = g_font_mgr.m_default_font;
    float gap_logo = 84.0f * scale, gap_disc = 38.0f * scale;
    float block = logo_h + gap_logo + 2 * r + gap_disc + f_title->FontSize +
                  10.0f * scale + f_name->FontSize + 14.0f * scale +
                  f_small->FontSize;
    float y = p0.y + std::max(16.0f * scale, (H - block) / 2);

    // Gentle rise as it appears
    y += (1.0f - a) * 12.0f * scale;

    if (logo_h > 0) {
        dl->AddImage((ImTextureID)(intptr_t)g_xbemu_logo_tex,
                     ImVec2(cx - logo_w / 2, y),
                     ImVec2(cx + logo_w / 2, y + logo_h), ImVec2(0, 0),
                     ImVec2(1, 1), Col(255, 255, 255, a));
    }
    y += logo_h + gap_logo;

    // A soft green glow behind the disc
    ImVec2 c(cx, y + r);
    for (int i = 3; i >= 1; i--) {
        dl->AddCircleFilled(c, r * (1.0f + 0.16f * i),
                            Col(30, 160, 40, 0.05f * a), 64);
    }
    DrawDisc(dl, c, r, s_angle, a);

    // Green progress arc around the disc while loading
    float ring = r + 10.0f * scale;
    dl->AddCircle(c, ring, Col(255, 255, 255, 0.06f * a), 96, 3.0f * scale);
    if (!done) {
        float s0 = (float)(t * 3.2);
        float len = 1.1f + 0.6f * sinf((float)(t * 2.1));
        dl->PathArcTo(c, ring, s0, s0 + len, 40);
        dl->PathStroke(Col(60, 200, 70, a), 0, 3.0f * scale);
    }

    // Loaded: a green badge with a tick pops onto the disc
    if (done) {
        float k = std::min(1.0f, (float)((t - s_loaded_at) / 0.28));
        float pop = 1.0f + 0.25f * sinf(k * (float)M_PI); // a little overshoot
        float br = r * 0.42f * k * pop;
        ImVec2 b(c.x + r * 0.68f, c.y + r * 0.68f);
        if (br > 1.0f) {
            dl->AddCircleFilled(ImVec2(b.x, b.y + 2 * scale), br,
                                Col(0, 0, 0, 0.4f * a), 32);
            dl->AddCircleFilled(b, br, Col(30, 170, 40, a), 32);
            dl->AddCircle(b, br, Col(255, 255, 255, 0.5f * a), 32,
                          1.5f * scale);
            ImVec2 tick[3] = { ImVec2(b.x - br * 0.45f, b.y + br * 0.02f),
                               ImVec2(b.x - br * 0.10f, b.y + br * 0.36f),
                               ImVec2(b.x + br * 0.48f, b.y - br * 0.32f) };
            dl->AddPolyline(tick, 3, Col(255, 255, 255, a), 0,
                            std::max(1.5f, br * 0.16f));
        }
    }
    y += 2 * r + gap_disc;

    // "Loading disc..." with the dots counting up, placed as if all three
    // were there so the words don't shift
    if (done) {
        y += CentredText(dl, f_title, cx, y, Col(110, 220, 120, a),
                         "Disc loaded");
    } else {
        // The dots count up: the translation without its own dots
        std::string word = Tr("Loading disc...");
        if (word.size() > 3 && !word.compare(word.size() - 3, 3, "...")) {
            word.resize(word.size() - 3);
        }
        const char *full = "Loading disc...";
        ImVec2 sz = f_title->CalcTextSizeA(f_title->FontSize, FLT_MAX, 0,
                                           full);
        int dots = (int)(t * 2.5) % 4;
        char buf[96];
        snprintf(buf, sizeof(buf), "%s%.*s", word.c_str(), dots, "...");
        dl->AddText(f_title, f_title->FontSize, ImVec2(cx - sz.x / 2, y),
                    Col(205, 214, 208, a), buf);
        y += sz.y;
    }
    y += 10.0f * scale;
    y += CentredText(dl, f_name, cx, y, Col(255, 255, 255, a),
                     s_name.c_str());
    y += 14.0f * scale;
    CentredText(dl, f_small, cx, y, Col(125, 138, 130, a),
                done ? "Ready on the XBL Dashboard" :
                       "Updating the XBL Dashboard");
}
