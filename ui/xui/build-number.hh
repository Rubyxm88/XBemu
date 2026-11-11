//
// xemu User Interface: XBemu build numbers
//
// XBemu builds are "v0.1 build 7": the build number goes up by one with
// every release, stable or beta, and the tag is xbemu-7. Whether a build is
// a beta is the channel= line of its version file and the pre-release mark
// on GitHub, not its number. Version files say "build=7".
//
// The older numbering is still read: sub builds (26.1, "sub=1") and betas
// with a three-part number (27.3.1, "beta=1"), as Black Arrow Online copies
// have them. Inside the program one number holds all three:
// build * 10000 + sub * 100 + beta (70000 for 7, 270301 for 27.3.1), so
// comparing two builds stays a plain comparison.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
#pragma once

#include <string>
#include <stdlib.h>

/* Each level (sub build, beta) goes up to 99 */
#define BUILD_SUBS 100
#define BUILD_WHOLE_UNIT (BUILD_SUBS * BUILD_SUBS)

// The whole build: 26 for 26, 26.1 and 26.1.2
static inline int BuildWhole(int code)
{
    return code / BUILD_WHOLE_UNIT;
}

// The sub build: 1 for 26.1 and 26.1.2
static inline int BuildSub(int code)
{
    return code / BUILD_SUBS % BUILD_SUBS;
}

// The beta: 2 for 26.1.2, 0 for a build that is not a beta
static inline int BuildBeta(int code)
{
    return code % BUILD_SUBS;
}

static inline bool BuildIsBeta(int code)
{
    return BuildBeta(code) != 0;
}

// From the build=, sub= and beta= values of a version file; 0 for a test
// build
static inline int BuildFromFields(const std::string &build,
                                  const std::string &sub,
                                  const std::string &beta = "")
{
    int whole = atoi(build.c_str());
    int part = atoi(sub.c_str());
    int b = atoi(beta.c_str());
    if (whole <= 0) {
        return 0;
    }
    if (part < 0 || part >= BUILD_SUBS) {
        part = 0;
    }
    if (b < 0 || b >= BUILD_SUBS) {
        b = 0;
    }
    return whole * BUILD_WHOLE_UNIT + part * BUILD_SUBS + b;
}

// From "26", "26.1" or "27.3.1" (update-skip.txt, update-done.txt, a release
// tag without its "xbemu-"); 0 when it is none of them
static inline int BuildFromText(const std::string &text)
{
    std::string parts[3];
    int n = 0;
    size_t pos = 0;
    while (true) {
        if (n == 3) {
            return 0;
        }
        size_t dot = text.find('.', pos);
        parts[n++] = text.substr(pos, dot == std::string::npos ?
                                          std::string::npos : dot - pos);
        if (dot == std::string::npos) {
            break;
        }
        pos = dot + 1;
    }
    for (int i = 0; i < n; i++) {
        std::string &s = parts[i];
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                              s.back() == ' ')) {
            s.pop_back();
        }
        if (s.empty() || s.find_first_not_of("0123456789") !=
                             std::string::npos || s.size() > 4) {
            return 0;
        }
    }
    return BuildFromFields(parts[0], parts[1], parts[2]);
}

// "26", "26.1", "27.3.1" (a beta of 27.3) or "27.0.1" (a beta of 27)
static inline std::string BuildText(int code)
{
    std::string text = std::to_string(BuildWhole(code));
    if (BuildSub(code) || BuildBeta(code)) {
        text += "." + std::to_string(BuildSub(code));
    }
    if (BuildBeta(code)) {
        text += "." + std::to_string(BuildBeta(code));
    }
    return text;
}

// The release tag: xbemu-7 (older: xbemu-26.1, xbemu-27.3.1)
static inline std::string BuildTag(int code)
{
    return "xbemu-" + BuildText(code);
}
