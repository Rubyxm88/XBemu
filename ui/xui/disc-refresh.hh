//
// xemu User Interface: show a newly loaded disc on the XBL Dashboard
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
#pragma once

// After the window shows the console's picture: keeps a blurred copy of it
// when a refresh is starting
void DiscRefreshCapture(int width, int height);

// Each frame: the overlay, and the presses that make the dashboard look
void DiscRefreshDraw();

extern "C" {
// A disc was loaded: on the XBL Dashboard, cover the screen, move right and
// back left so the dashboard reads the new disc, then show it. Does nothing
// while a game runs.
void xemu_disc_refresh_start(const char *path);
}
