//
// xemu User Interface: OXOS BIOS updates
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Looks for new OXOS releases (github.com/kooscode/oxos) once a day, asks
// the player before updating, and keeps the last few versions so Machine >
// BIOS can switch back. The player's choice is kept in
// system/bios/oxos-choice.txt, which the Windows launcher and the Mac and
// Linux setup also read.
//
#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <map>
#include <glib.h>

class OxosWindow
{
public:
    OxosWindow();
    void Draw();
    // Machine > BIOS
    void Menu();
    // Looks now (the player asked), even if it looked today
    void CheckNow();

private:
    enum Status { IDLE, CHECKING, CHECKED, DOWNLOADING, DOWNLOADED, FAILED };
    enum Prompt { NONE, OFFER, PROGRESS, RESTART, SWITCH, ROLLBACK };

    struct Release {
        std::string tag, zip_url, digest;
    };

    std::atomic<int> m_status;
    std::atomic<int> m_progress;   // percent while downloading
    bool             m_started;
    bool             m_asked;      // the player asked for this check
    double           m_start_time;
    double           m_ran;        // seconds the console ran on a new BIOS
    Prompt           m_prompt;
    std::string      m_error;
    std::string      m_switch_to;  // Machine > BIOS choice waiting to confirm

    // Results of the check (written by its thread before CHECKED)
    std::vector<Release> m_releases;          // newest first
    std::map<std::string, std::string> m_notes;  // tag -> tested / broken: ...
    // Written by the check thread, taken over by AfterCheck
    std::vector<Release> m_found;
    std::map<std::string, std::string> m_found_notes;
    Release          m_offer;                 // what Update installs
    std::string      m_downloaded;            // tag

    static gpointer CheckThread(gpointer self);
    static gpointer DownloadThread(gpointer self);
    void StartCheck(bool asked);
    void StartDownload(const Release &r);
    void AfterCheck();
    void DrawOffer();
    void DrawProgress();
    void DrawRestart();
    void DrawSwitch();
    void DrawRollback();
    bool Newest(Release *out);
};

extern OxosWindow oxos_window;
