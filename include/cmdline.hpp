// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_CMDLINE_HPP
#define LCPROJECTS_TSUBAKI_CMDLINE_HPP

#include "argparser.hpp"

#include <filesystem>
#include <ostream>

namespace tsubaki
{
namespace cmdline_group
{
inline constexpr const char *general = "general";
inline constexpr const char *sum = "sum";
} // namespace cmdline_group

namespace cmdline_entry
{
inline constexpr const char *subcmd = "subcmd";
inline constexpr const char *version = "version";
inline constexpr const char *help = "help";
inline constexpr const char *key = "key";
inline constexpr const char *key_file = "key_file";
inline constexpr const char *threads = "threads";
inline constexpr const char *output = "output";
inline constexpr const char *follow_symlinks = "follow-symlinks";
inline constexpr const char *force_rescan = "force-rescan";
inline constexpr const char *verbose = "verbose";
inline constexpr const char *algorithm = "algorithm";
inline constexpr const char *excludes = "excludes";
inline constexpr const char *lists = "lists";
inline constexpr const char *files = "files";
} // namespace cmdline_entry

namespace subcommands
{
inline constexpr const char *sum = "sum";
} // namespace subcommands

using namespace argparser;

static inline void error_prompt(std::ostream &os, ap_instance &cmdline)
{
    const std::string program =
        std::filesystem::path(cmdline.argv_zero()).filename().string();
    for (auto &err : cmdline.errors())
    {
        if (err.second == argparser::ap_result::invalid_arguments)
            os << "Error: invalid argument: " << err.first << std::endl;
        else
            os << "Error: " << argparser::describe(err.second) << ": "
               << cmdline.entry(err.first).explanations.at(0) << std::endl;
    }
    os << "Try '" << program << " --help' for more information." << std::endl;
}

static inline ap_instance def_args()
{
    // clang-format off
    return ap_instance(
        {
            ap_entry( // Handled in src/main.cpp
                cmdline_entry::subcmd,
                ap_type::positional,
                {
                    {ap_defkey::group, cmdline_group::general},
                    {ap_defkey::explanations,"subcommand"},
                    {ap_defkey::min_positional_count,"1"},
                    {ap_defkey::max_positional_count,"1"},
                }
            ),
            ap_entry( // Handled in src/main.cpp
                cmdline_entry::version,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::general},
                    {ap_defkey::explanations,"-V or --version"},
                    {ap_defkey::long_name,"version"},
                    {ap_defkey::short_name,"V"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/main.cpp
                cmdline_entry::help,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::general},
                    {ap_defkey::explanations,"-h or --help"},
                    {ap_defkey::long_name,"help"},
                    {ap_defkey::short_name,"h"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::excludes,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-e or --exclude(s)"},
                    {ap_defkey::long_name,"exclude,excludes"},
                    {ap_defkey::short_name,"e"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::lists,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-l or --list(s)"},
                    {ap_defkey::long_name,"list,lists"},
                    {ap_defkey::short_name,"l"},
                }
            ),
            ap_entry( // Handled in src/main.cpp
                cmdline_entry::key_file,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-K or --key-file"},
                    {ap_defkey::long_name,"key-file"},
                    {ap_defkey::short_name,"K"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry( // Handled in src/main.cpp
                cmdline_entry::key,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-k or --key"},
                    {ap_defkey::long_name,"key"},
                    {ap_defkey::short_name,"k"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::threads,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-t or --thread(s)"},
                    {ap_defkey::long_name,"thread,threads"},
                    {ap_defkey::short_name,"t"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::output,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-o or --output"},
                    {ap_defkey::long_name,"output"},
                    {ap_defkey::short_name,"o"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::follow_symlinks,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-s or --follow-symlinks"},
                    {ap_defkey::long_name,"follow-symlinks"},
                    {ap_defkey::short_name,"s"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::force_rescan,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-f or --force-rescan"},
                    {ap_defkey::long_name,"force-rescan"},
                    {ap_defkey::short_name,"f"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::verbose,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"-v or --verbose"},
                    {ap_defkey::long_name,"verbose"},
                    {ap_defkey::short_name,"v"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::algorithm,
                ap_type::positional,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"algorithm"},
                    {ap_defkey::min_positional_count,"1"},
                    {ap_defkey::max_positional_count,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::files,
                ap_type::positional,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::explanations,"files or directories"},
                }
            ),
        }
    );
    //clang-format on
}
}
#endif
