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
#include <algorithm>
#include <string>
#include <vector>
#include <filesystem>
#include "common.hh"
#include "disc-refresh.hh"
#include "font-manager.hh"
#include "gl-helpers.hh"
#include "viewport-manager.hh"
#include "../xemu-input.h"
#include "../xemu-running-title.h"
#include "../xemu-dashinstall.h"
#include "../xemu-gamecache.h"

// Seconds
#define FADE_IN      0.35
#define FIRST_PRESS  0.80   // after the disc change
#define PRESS_HOLD   0.20
#define SETTLE       0.70   // after a press: the picture shows the move
#define MOUNT_WAIT   2.00   // after a way back: time to mount the disc
#define LOADED_TIME  1.00   // "Disc loaded" on screen
#define FADE_TIME    0.45
#define DUCK_RAMP    0.20   // the sound fades out before the first press

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
    s_before.clear();
    s_mounts = __atomic_load_n(&xemu_atapi_capacity_reads, __ATOMIC_RELAXED);
    SetPhase(PHASE_MOVE, 0);
    fprintf(stderr, "disc refresh: start (%s)\n", s_name.c_str());
}

// A spinning arc, or a green circle with a tick when done
static void DrawMark(bool done, double t, float size)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c(p.x + size / 2, p.y + size / 2);
    float r = size / 2;
    float scale = g_viewport_mgr.m_scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (done) {
        dl->AddCircleFilled(c, r, IM_COL32(26, 158, 26, 255));
        ImVec2 tick[3] = { ImVec2(c.x - r * 0.48f, c.y + r * 0.04f),
                           ImVec2(c.x - r * 0.12f, c.y + r * 0.40f),
                           ImVec2(c.x + r * 0.52f, c.y - r * 0.36f) };
        dl->AddPolyline(tick, 3, IM_COL32_WHITE, 0, 3.0f * scale);
    } else {
        dl->AddCircle(c, r - 2 * scale, IM_COL32(255, 255, 255, 40), 0,
                      3.0f * scale);
        float a0 = (float)(t * 5.0);
        dl->PathArcTo(c, r - 2 * scale, a0, a0 + 4.2f, 32);
        dl->PathStroke(IM_COL32(40, 190, 40, 255), 0, 3.0f * scale);
    }
    ImGui::Dummy(ImVec2(size, size));
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
    // "Disc loaded" (a click is still queued for a moment after its press),
    // so the moves make no menu sounds
    float duck;
    if (s_loaded_at < 0 || t < s_loaded_at) {
        duck = (float)(1.0 - (t - (FIRST_PRESS - DUCK_RAMP)) / DUCK_RAMP);
    } else {
        duck = (float)((t - s_loaded_at) / DUCK_RAMP);
    }
    SetDuck(std::min(1.0f, std::max(0.0f, duck)));

    double fade_out = s_loaded_at < 0 ? 1e9 : s_loaded_at + LOADED_TIME;
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

    // The cover, as the XBL Dashboard setup's
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
        IM_COL32(12, 14, 12, (int)(255 * a)));

    bool done = s_phase == PHASE_LOADED && t >= s_loaded_at;
    float scale = g_viewport_mgr.m_scale;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    if (ImGui::Begin("##disc-refresh", NULL,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
        float lw = std::min(vp->Size.x * 0.6f, 420.0f * scale);
        if (g_xbemu_logo_tex && g_xbemu_logo_w > 0) {
            float lh = lw * g_xbemu_logo_h / g_xbemu_logo_w;
            ImGui::Image((ImTextureID)(intptr_t)g_xbemu_logo_tex,
                         ImVec2(lw, lh));
            ImGui::Dummy(ImVec2(0, 22.0f * scale));
        } else {
            ImGui::Dummy(ImVec2(lw, 0));
        }

        // Mark and words, centred under the logo
        ImGui::PushFont(g_font_mgr.m_menu_font);
        const char *title = done ? "Disc loaded" : "Loading disc...";
        float size = ImGui::GetTextLineHeight() * 1.5f;
        ImVec2 tsz = ImGui::CalcTextSize(title);
        ImGui::PopFont();
        ImGui::PushFont(g_font_mgr.m_default_font);
        ImVec2 nsz = ImGui::CalcTextSize(s_name.c_str());
        ImGui::PopFont();
        float gap = 14.0f * scale;
        float row = size + gap + std::max(tsz.x, nsz.x);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f, (lw - row) / 2));
        DrawMark(done, t, size);
        ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        ImGui::PushFont(g_font_mgr.m_menu_font);
        ImGui::Text("%s", title);
        ImGui::PopFont();
        ImGui::PushFont(g_font_mgr.m_default_font);
        ImGui::TextColored(ImVec4(0.62f, 0.65f, 0.66f, 1.0f), "%s",
                           s_name.c_str());
        ImGui::PopFont();
        ImGui::EndGroup();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
