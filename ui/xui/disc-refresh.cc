//
// xemu User Interface: show a newly loaded disc on the XBL Dashboard
//
// The XBL Dashboard reads the disc only when the player moves in its menu,
// and OXOS keeps the old disc mounted until a read fails. So after a disc
// change the first move shows "no disc" and the second the new disc. Here
// xemu makes both moves itself, a move and its way back so the cursor ends
// where the player left it, under a blurred still of the screen with
// "Loading disc...", so the player sees neither the moves nor the "no disc"
// in between, then "Disc loaded" and the dashboard with the new disc.
//
// xemu cannot see where the cursor is. A move the dashboard took makes it
// look at the drive; one it could not take (the end of a row) does not. So
// after each first move xemu watches the drive: nothing there, and it tries
// the next pair (right/left, left/right, down/up, up/down).
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
#include <epoxy/gl.h>
#include <math.h>
#include <algorithm>
#include <string>
#include <filesystem>
#include "common.hh"
#include "disc-refresh.hh"
#include "font-manager.hh"
#include "viewport-manager.hh"
#include "../xemu-input.h"
#include "../xemu-running-title.h"
#include "../xemu-dashinstall.h"
#include "../xemu-gamecache.h"

// Seconds
#define FADE_IN      0.30
#define FIRST_PRESS  0.80   // after the disc change
#define PRESS_HOLD   0.25
#define WATCH        0.90   // after a press: did the dashboard read the drive
#define BACK_GAP     0.60   // then the way back (it mounts the new disc)
#define MOUNT_TIME   1.00   // after the way back, before "Disc loaded"
#define LOADED_TIME  1.00   // "Disc loaded" on screen
#define FADE_TIME    0.40

extern "C" uint32_t xemu_atapi_commands; // hw/ide/atapi.c
extern "C" float xemu_audio_duck;        // hw/xbox/mcpx/apu/monitor.c
#define DUCK_RAMP 0.20 // the menu sounds fade out before the first press

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

// A dashboard must have run this long (ms), so a restart's leftovers don't
// count
#define DASH_UP_MS 3000

static bool s_active;
static double s_start;          // ImGui time; < 0 until the first frame
static std::string s_name;
static uint16_t s_pressed;
static int s_pair;             // which pair is being tried
static bool s_moved;           // its first move was taken; next: the way back
static double s_next_press;    // when the next press starts (seconds)
static double s_press_at = -1; // when the last press started; < 0: none
static uint32_t s_commands;    // drive commands before that press
static double s_loaded_at = -1; // "Disc loaded" from then on
static bool s_capture_pending;
static bool s_have_still;

// Two textures: a quarter of the window, then a 24th (the blur)
static GLuint s_tex[2], s_fbo[2];
static int s_w[2], s_h[2];

static void EnsureTarget(int i, int w, int h)
{
    if (s_tex[i] && s_w[i] == w && s_h[i] == h) {
        return;
    }
    if (!s_tex[i]) {
        glGenTextures(1, &s_tex[i]);
        glGenFramebuffers(1, &s_fbo[i]);
    }
    glBindTexture(GL_TEXTURE_2D, s_tex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s_tex[i], 0);
    s_w[i] = w;
    s_h[i] = h;
}

void DiscRefreshCapture(int width, int height)
{
    if (!s_capture_pending || width <= 0 || height <= 0) {
        return;
    }
    s_capture_pending = false;

    GLint draw_fb = 0, read_fb = 0, tex = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fb);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    while (glGetError() != GL_NO_ERROR) {
    }

    int w0 = std::max(width / 4, 1), h0 = std::max(height / 4, 1);
    int w1 = std::max(width / 24, 1), h1 = std::max(height / 24, 1);
    EnsureTarget(0, w0, h0);
    EnsureTarget(1, w1, h1);

    // The window as shown, scaled down twice with filtering: a soft blur
    glBindFramebuffer(GL_READ_FRAMEBUFFER, draw_fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_fbo[0]);
    glBlitFramebuffer(0, 0, width, height, 0, 0, w0, h0, GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo[0]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_fbo[1]);
    glBlitFramebuffer(0, 0, w0, h0, 0, 0, w1, h1, GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);
    s_have_still = glGetError() == GL_NO_ERROR;
    if (!s_have_still) {
        fprintf(stderr, "disc refresh: no still of the screen, dark cover "
                        "only\n");
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fb);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
    }
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

// The presses, one at a time: each held PRESS_HOLD s, then WATCH s to see
// whether the dashboard read the drive
static void Step(double t)
{
    if (s_loaded_at >= 0) {
        return;
    }
    if (s_press_at >= 0) {
        if (s_pressed && t >= s_press_at + PRESS_HOLD) {
            Release();
        }
        if (t < s_press_at + WATCH) {
            return;
        }
        bool read = __atomic_load_n(&xemu_atapi_commands, __ATOMIC_RELAXED) != s_commands;
        s_press_at = -1;
        if (s_moved) {
            // The way back: the dashboard mounts the new disc now
            fprintf(stderr, "disc refresh: back%s\n",
                    read ? "" : " (no drive read)");
            s_loaded_at = t + MOUNT_TIME;
            return;
        }
        if (read) {
            s_moved = true;
            s_next_press = t + BACK_GAP;
        } else if (++s_pair < 4) {
            fprintf(stderr, "disc refresh: not taken, trying %s\n",
                    pair_names[s_pair][0]);
            s_next_press = t;
        } else {
            fprintf(stderr, "disc refresh: no move taken\n");
            s_loaded_at = t;
            return;
        }
    }
    if (t >= s_next_press) {
        int i = s_moved ? 1 : 0;
        s_commands = __atomic_load_n(&xemu_atapi_commands, __ATOMIC_RELAXED);
        s_press_at = t;
        Press(pairs[s_pair][i]);
        fprintf(stderr, "disc refresh: %s\n", pair_names[s_pair][i]);
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
    s_moved = false;
    s_next_press = FIRST_PRESS;
    s_press_at = -1;
    s_loaded_at = -1;
    s_capture_pending = true;
    s_have_still = false;
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
        dl->PathStroke(IM_COL32(40, 190, 40, 255), 0,
                       3.0f * scale);
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
    // "Disc loaded" (well after the last press: its click is still queued
    // for a moment), so the moves make no menu sounds
    float duck;
    if (s_loaded_at < 0 || t < s_loaded_at) {
        duck = (float)(1.0 - (t - (FIRST_PRESS - DUCK_RAMP)) / DUCK_RAMP);
    } else {
        duck = (float)((t - s_loaded_at) / DUCK_RAMP);
    }
    duck = std::min(1.0f, std::max(0.0f, duck));
    SetDuck(duck);

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

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImVec2 p0 = vp->Pos, p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    ImDrawList *bg = ImGui::GetBackgroundDrawList();
    if (s_have_still) {
        // The blurred still, plus four half-texel taps to soften it
        ImTextureID id = (ImTextureID)(intptr_t)s_tex[1];
        ImVec2 uv0(0, 1), uv1(1, 0); // GL rows run bottom up
        bg->AddImage(id, p0, p1, uv0, uv1,
                     IM_COL32(255, 255, 255, (int)(255 * a)));
        float dx = vp->Size.x / s_w[1] * 0.5f, dy = vp->Size.y / s_h[1] * 0.5f;
        const float offs[4][2] = { { -dx, -dy }, { dx, -dy },
                                   { -dx, dy }, { dx, dy } };
        for (auto &o : offs) {
            bg->AddImage(id, ImVec2(p0.x + o[0], p0.y + o[1]),
                         ImVec2(p1.x + o[0], p1.y + o[1]), uv0, uv1,
                         IM_COL32(255, 255, 255, (int)(60 * a)));
        }
        bg->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, (int)(110 * a)));
    } else {
        bg->AddRectFilled(p0, p1, IM_COL32(8, 10, 8, (int)(235 * a)));
    }

    // The card in the middle
    bool done = s_loaded_at >= 0 && t >= s_loaded_at;
    float scale = g_viewport_mgr.m_scale;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(26.0f * scale, 20.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.90f);
    if (ImGui::Begin("##disc-refresh", NULL,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(g_font_mgr.m_menu_font);
        float size = ImGui::GetTextLineHeight() * 1.6f;
        DrawMark(done, t, size);
        ImGui::SameLine(0, 16.0f * scale);
        ImGui::BeginGroup();
        ImGui::Text("%s", done ? "Disc loaded" : "Loading disc...");
        ImGui::PopFont();
        ImGui::PushFont(g_font_mgr.m_default_font);
        ImGui::TextColored(ImVec4(0.72f, 0.80f, 0.72f, 1.0f), "%s",
                           s_name.c_str());
        ImGui::PopFont();
        ImGui::EndGroup();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}
