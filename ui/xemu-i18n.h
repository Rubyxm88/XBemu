/*
 * XBemu languages for C code: the translation of an English text in the
 * language XBemu shows now (ui/xui/i18n.hh), or the text itself. Safe from
 * any thread; the result is never freed.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XEMU_I18N_H
#define XEMU_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

const char *xemu_i18n_tr(const char *english);
/* True once the menus know their language (after the first frame) */
int xemu_i18n_ready(void);

#ifdef __cplusplus
}
#endif

#endif
