// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_HELP_HPP
#define LCPROJECTS_TSUBAKI_HELP_HPP

#include <string>
#include <string_view>

// Injected by CMake through target_compile_definitions(). The fallback keeps
// IDE builds and single-file compiles working.
#ifndef TSUBAKI_VERSION
#define TSUBAKI_VERSION "unknown"
#endif

namespace tsubaki
{
inline constexpr const char *version = TSUBAKI_VERSION;

std::string help_text(std::string_view program);
}

#endif
