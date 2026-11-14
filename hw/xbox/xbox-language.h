/*
 * The console's language, from its EEPROM (what dashboards set in their
 * settings and games follow)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef HW_XBOX_XBOX_LANGUAGE_H
#define HW_XBOX_XBOX_LANGUAGE_H

#include <stdbool.h>
#include <stdint.h>

/* XC_LANGUAGE values */
#define XBOX_LANGUAGE_ENGLISH 1
#define XBOX_LANGUAGE_JAPANESE 2
#define XBOX_LANGUAGE_GERMAN 3
#define XBOX_LANGUAGE_FRENCH 4
#define XBOX_LANGUAGE_SPANISH 5
#define XBOX_LANGUAGE_ITALIAN 6
#define XBOX_LANGUAGE_KOREAN 7
#define XBOX_LANGUAGE_CHINESE 8
#define XBOX_LANGUAGE_PORTUGUESE 9

#ifdef __cplusplus
extern "C" {
#endif

/* The language in the running EEPROM, as the console last wrote it. False
 * when there is no EEPROM yet. */
bool xbox_language_get(uint32_t *language);

#ifdef __cplusplus
}
#endif

#endif
