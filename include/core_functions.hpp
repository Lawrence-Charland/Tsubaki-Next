// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_CORE_FUNCTIONS_HPP
#define LCPROJECTS_TSUBAKI_CORE_FUNCTIONS_HPP

#include "argparser.hpp"

namespace tsubaki
{
int sum_main(argparser::ap_instance &cmdline, bool internal = false);
int verify_main(argparser::ap_instance &cmdline);
int cmp_main(argparser::ap_instance &cmdline);
} // namespace tsubaki

#endif
