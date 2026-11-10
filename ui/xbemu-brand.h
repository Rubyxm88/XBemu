/*
 * XBemu: product name, GitHub repository and release file names in one place
 *
 * Copyright (c) 2026 XBemu contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XBEMU_BRAND_H
#define XBEMU_BRAND_H

#define XBEMU_NAME "XBemu"

/* owner/name of the GitHub repository that publishes XBemu releases */
#define XBEMU_REPO "Rubyxm88/XBemu"
#define XBEMU_RELEASES_PAGE "https://github.com/" XBEMU_REPO "/releases"
#define XBEMU_RELEASES_API "https://api.github.com/repos/" XBEMU_REPO "/releases"

/* Release files */
#define XBEMU_WIN_UPDATE_ZIP "XBemu-update.zip"
#define XBEMU_MAC_UPDATE_ZIP "XBemu-mac-update.zip"
#define XBEMU_LINUX_UPDATE_TGZ "XBemu-linux-update.tar.gz"

/* What the player starts */
#define XBEMU_LAUNCHER_EXE "XBemu.exe"
#define XBEMU_MAC_APP "XBemu.app"

/* Black Arrow Online, the project XBemu came from: a moved copy keeps its
 * data folder on a Mac or Linux */
#define XBEMU_OLD_NAME "Black Arrow Online"

#endif
