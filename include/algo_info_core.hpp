// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_ALGO_INFO_CORE_HPP
#define LCPROJECTS_TSUBAKI_ALGO_INFO_CORE_HPP

#include "argparser.hpp"
#include "checksum.hpp"
#include <string_view>

namespace tsubaki
{
std::string_view get_info_builtin(algorithm algo);

int algo_info_core(argparser::ap_instance &cmdline);
} // namespace tsubaki

#endif