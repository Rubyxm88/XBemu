/*
 * xemu running title
 *
 * Which game the console is running, read from the XBE headers in guest
 * memory, so a game started from a dashboard's hard disk counts as well as
 * one on a disc. Dashboards and other system titles (title ids 0xFFFE0000
 * and up) and XBEs without a title id do not count as a game.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XEMU_RUNNING_TITLE_H
#define XEMU_RUNNING_TITLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XemuRunningTitle {
    uint32_t title_id;
    uint32_t version;
    char name[41];
} XemuRunningTitle;

/* Main thread, once a frame: looks at guest memory at most once a second */
void xemu_running_title_poll(void);

/* Any thread: the game running now; false while none is (or a dashboard) */
bool xemu_running_title_get(XemuRunningTitle *out);

#ifdef __cplusplus
}
#endif

#endif
