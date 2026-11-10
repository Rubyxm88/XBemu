//
// xemu User Interface: XBemu build numbers with sub builds
// and betas
//
// A build is a whole number (26), a sub build of it (26.1, 26.2...) that
// updates only some systems, or a beta of either (27.3.1, 27.3.2...) that
// only players on the beta channel get. Version files keep "build=26", which
// every older launcher and app reads as a whole number, and add "sub=1" for a
// sub build and "beta=1" for a beta; release tags are build-26.1 and
// build-27.3.1. Inside the program one number holds all three:
// build * 10000 + sub * 100 + beta (260100 for 26.1, 270301 for 27.3.1), so
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

// The release tag: build-26, build-26.1 or build-27.3.1
static inline std::string BuildTag(int code)
{
    return "xbemu-" + BuildText(code);
}
