// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_CMDLINE_HPP
#define LCPROJECTS_TSUBAKI_CMDLINE_HPP

#include "argparser.hpp"

namespace tsubaki
{
static inline argparser::ap_instance def_args()
{
    // clang-format off
    return argparser::ap_instance(
        {
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "threads",
                argparser::ap_type::option,
                {
                    {argparser::ap_defkey::explanations,"count of the worker threads"},
                    {argparser::ap_defkey::explanations,"-t or --thread(s)"},
                    {argparser::ap_defkey::long_name,"thread,threads"},
                    {argparser::ap_defkey::short_name,"t"},
                    {argparser::ap_defkey::max_value_count,"1"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "output",
                argparser::ap_type::option,
                {
                    {argparser::ap_defkey::explanations,"output file"},
                    {argparser::ap_defkey::explanations,"-o or --output"},
                    {argparser::ap_defkey::long_name,"output"},
                    {argparser::ap_defkey::short_name,"o"},
                    {argparser::ap_defkey::max_value_count,"1"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "follow-symlinks",
                argparser::ap_type::flag,
                {
                    {argparser::ap_defkey::explanations,"follow the symlinks of directories"},
                    {argparser::ap_defkey::explanations,"-s or --follow-symlinks"},
                    {argparser::ap_defkey::long_name,"follow-symlinks"},
                    {argparser::ap_defkey::short_name,"s"},
                    {argparser::ap_defkey::max_repetition,"1"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "force-rescan",
                argparser::ap_type::flag,
                {
                    {argparser::ap_defkey::explanations,"recompute the existing checksums"},
                    {argparser::ap_defkey::explanations,"-f or --force-rescan"},
                    {argparser::ap_defkey::long_name,"force-rescan"},
                    {argparser::ap_defkey::short_name,"f"},
                    {argparser::ap_defkey::max_repetition,"1"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "verbose",
                argparser::ap_type::flag,
                {
                    {argparser::ap_defkey::explanations,"enable progress printer or not"},
                    {argparser::ap_defkey::explanations,"-v or --verbose"},
                    {argparser::ap_defkey::long_name,"verbose"},
                    {argparser::ap_defkey::short_name,"v"},
                    {argparser::ap_defkey::max_repetition,"1"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "algorithm",
                argparser::ap_type::positional,
                {
                    {argparser::ap_defkey::explanations,"algorithm to use"},
                    {argparser::ap_defkey::explanations,"algorithm"},
                    {argparser::ap_defkey::min_positional_count,"1"},
                    {argparser::ap_defkey::max_positional_count,"1"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "excludes",
                argparser::ap_type::option,
                {
                    {argparser::ap_defkey::explanations,"excluded files or directories while scanning dirs"},
                    {argparser::ap_defkey::explanations,"-e or --exclude(s)"},
                    {argparser::ap_defkey::long_name,"exclude,excludes"},
                    {argparser::ap_defkey::short_name,"e"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "lists",
                argparser::ap_type::option,
                {
                    {argparser::ap_defkey::explanations,"textual file lists to scan"},
                    {argparser::ap_defkey::explanations,"-l or --list(s)"},
                    {argparser::ap_defkey::long_name,"list,lists"},
                    {argparser::ap_defkey::short_name,"l"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "files",
                argparser::ap_type::positional,
                {
                    {argparser::ap_defkey::explanations,"files or directories to be calculated"},
                    {argparser::ap_defkey::explanations,"files or directories"},
                    {argparser::ap_defkey::barrier,"\\"},
                }
            ),
            argparser::ap_entry(
                "version",
                argparser::ap_type::flag,
                {
                    {argparser::ap_defkey::explanations,"get the version"},
                    {argparser::ap_defkey::explanations,"-V or --version"},
                    {argparser::ap_defkey::long_name,"version"},
                    {argparser::ap_defkey::short_name,"V"},
                    {argparser::ap_defkey::max_repetition,"1"},
                }
            ),
            argparser::ap_entry( // Done in src/tsubaki_core.cpp
                "help",
                argparser::ap_type::flag,
                {
                    {argparser::ap_defkey::explanations,"get the help information"},
                    {argparser::ap_defkey::explanations,"-h or --help"},
                    {argparser::ap_defkey::long_name,"help"},
                    {argparser::ap_defkey::short_name,"h"},
                    {argparser::ap_defkey::max_repetition,"1"},
                }
            )
        }
    );
    //clang-format on
}
}
#endif
