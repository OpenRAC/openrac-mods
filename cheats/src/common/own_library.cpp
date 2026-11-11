// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Where the mod's own library is (settings_file.h, own_library): the system's loader knows the file
// of the code at an address. Kept apart so that windows.h meets none of the mod's names.
#include <filesystem>
#include <optional>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <vector>
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cheats {

std::optional<std::filesystem::path> own_library() {
#if defined(_WIN32)
    HMODULE module = nullptr;

    // The module holding this very function.
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&own_library),
            &module
        )) {
        return std::nullopt;
    }

    std::vector<wchar_t> name(32768);
    const DWORD length = GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));

    // No name, or one longer than the longest path.
    if (length == 0 || length >= name.size()) {
        return std::nullopt;
    }

    return std::filesystem::path(std::wstring(name.data(), length));
#else
    Dl_info info{};

    // The object holding this very function, and its file.
    if (dladdr(reinterpret_cast<void*>(&own_library), &info) == 0 || info.dli_fname == nullptr) {
        return std::nullopt;
    }

    return std::filesystem::path(info.dli_fname);
#endif
}

}  // namespace cheats
