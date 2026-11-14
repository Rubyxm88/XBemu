//
// xemu User Interface: XBemu in the console's language
//
// XBemu's menus follow the language the console is set to (the XBL Dashboard
// sets it in its settings; games follow it too). Text is translated where
// ImGui draws and measures it (subprojects/packagefiles/imgui), so the code
// keeps writing English and IDs stay English. The translations are one table,
// data/xbemu-lang.tsv: an English column and one column per language. Text
// with no translation shows in English.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
#pragma once

#include <string>
#include <glib.h>

// Load the table and hook ImGui; once, before the first frame
void I18nInit();

// Each frame before ImGui::NewFrame: follow the console's language
void I18nUpdate();

// "en", "de", "fr", "es", "it" or "pt"
const char *I18nCode();
// The language's own name ("Deutsch")
const char *I18nName();
bool I18nIsEnglish();

// The translation of an English text, for text built before ImGui gets it
// (formats for snprintf, std::string pieces); the text itself if none
const char *Tr(const char *english);
std::string TrS(const std::string &english);
// printf with the translated format (any thread; the format is looked up
// whole, so "OXOS %s is out" is one row of the table)
std::string TrF(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

// The notice shown once per language, and the report window
void I18nDraw();
void I18nOpenReport();
// Help > Language: Automatic or a fixed language (display.ui.language)
void I18nDrawLanguageMenu();

// Help menu: "Show me" opens it and flashes this item for a few seconds
extern const char *const kI18nReportItem;
bool I18nWantsHelpMenuOpen(); // true once, when the menu should open
bool I18nFlashReportItem();   // the item is flashing now
bool I18nKeepMenuBarAwake();
