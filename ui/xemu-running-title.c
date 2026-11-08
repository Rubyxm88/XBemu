/*
 * xemu running title
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "system/runstate.h"
#include "xemu-xbe.h"
#include "xemu-running-title.h"

#define POLL_MS 1000

static GMutex lock; /* a zeroed GMutex is ready to use */
static bool have;
static XemuRunningTitle current;
static int64_t last_poll;
static int64_t system_since; /* when a non-game XBE was first seen, or 0 */

static bool is_game(uint32_t title_id)
{
    return title_id != 0 && title_id < 0xFFFE0000;
}

void xemu_running_title_poll(void)
{
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    XemuRunningTitle found = { 0 };
    bool found_game = false;
    bool found_system = false;

    if (now - last_poll < POLL_MS) {
        return;
    }
    last_poll = now;

    if (runstate_is_running() || runstate_check(RUN_STATE_PAUSED)) {
        struct xbe *xbe = xemu_get_xbe_info();
        if (xbe && xbe->cert) {
            uint32_t id = ldl_le_p(&xbe->cert->m_titleid);
            if (is_game(id)) {
                found_game = true;
                found.title_id = id;
                found.version = ldl_le_p(&xbe->cert->m_version);
                for (int i = 0; i < 40; i++) {
                    uint16_t c = lduw_le_p(&xbe->cert->m_title_name[i]);
                    found.name[i] = c == 0 ? 0 : (c < 0x80 ? c : '?');
                    if (!c) {
                        break;
                    }
                }
                found.name[40] = 0;
            } else {
                found_system = true;
            }
        }
    }

    g_mutex_lock(&lock);
    if (!found_system) {
        system_since = 0;
    } else if (!system_since) {
        system_since = now;
    }
    if (found_game != have || (found_game &&
                               (found.title_id != current.title_id ||
                                found.version != current.version))) {
        if (found_game) {
            fprintf(stderr, "running title: %s (%08X, version %u)\n",
                    found.name, found.title_id, found.version);
        } else if (have) {
            fprintf(stderr, "running title: none (was %08X)\n",
                    current.title_id);
        }
    }
    have = found_game;
    current = found;
    g_mutex_unlock(&lock);
}

int64_t xemu_running_title_system_ms(void)
{
    int64_t r;

    g_mutex_lock(&lock);
    r = system_since ? qemu_clock_get_ms(QEMU_CLOCK_REALTIME) - system_since
                     : 0;
    g_mutex_unlock(&lock);
    return r;
}

bool xemu_running_title_get(XemuRunningTitle *out)
{
    bool r;

    g_mutex_lock(&lock);
    r = have;
    if (r && out) {
        *out = current;
    }
    g_mutex_unlock(&lock);
    return r;
}
