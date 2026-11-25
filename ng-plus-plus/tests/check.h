// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s test framework, the form of OpenRAC's port/tests/check.h (a mod cannot include the port's
// tests): CHECK records a failure and goes on, and main returns result().

#pragma once

#include <cstdio>

namespace ng_plus_plus::test {

// Returns the count of failed checks so far. Only the test's thread uses it.
inline int& failures() {
    static int count = 0;
    return count;
}

// Records a failed check of `condition` at `file`:`line`, with `context` (a disc, or "").
inline void fail(const char* file, int line, const char* context, const char* condition) {
    std::fprintf(stderr, "%s:%d: %s%s\n", file, line, context, condition);
    ++failures();
}

// Returns what main returns: 0 when every check passed. Prints `name: ok` then.
inline int result(const char* name) {
    // Quiet unless all passed: the failures are printed as they happen.
    if (failures() == 0) {
        std::printf("%s: ok\n", name);
    }

    return failures() == 0 ? 0 : 1;
}

}  // namespace ng_plus_plus::test

// Checks `condition`, recording a failure with its file and line; a macro because it needs them.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ::ng_plus_plus::test::fail(__FILE__, __LINE__, "", #condition);                        \
        }                                                                                          \
    } while (0)
