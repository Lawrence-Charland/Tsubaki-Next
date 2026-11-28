// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_CMDLINE_HPP
#define LCPROJECTS_TSUBAKI_CMDLINE_HPP

#include "argparser.hpp"

#include <filesystem>
#include <ostream>

namespace tsubaki
{
#define SUBCOMMAND_LIST(X)                                                     \
    X(sum)                                                                     \
    X(verify)                                                                  \
    X(cmp)                                                                     \
    X(algo)

#define CMDGROUP_LIST(X)                                                       \
    X(general)                                                                 \
    X(sum)                                                                     \
    X(sum_int)                                                                 \
    X(sum_greedy)                                                              \
    X(verify)                                                                  \
    X(cmp)                                                                     \
    X(algo_info)

#define CMDENTRY_LIST(X)                                                       \
    X(subcmd)                                                                  \
    X(version)                                                                 \
    X(list_alg)                                                                \
    X(help)                                                                    \
    X(comp)                                                                    \
    X(key)                                                                     \
    X(key_file)                                                                \
    X(text)                                                                    \
    X(threads)                                                                 \
    X(output)                                                                  \
    X(verbose)                                                                 \
    X(follow_symlinks)                                                         \
    X(force_rescan)                                                            \
    X(algorithm)                                                               \
    X(excludes)                                                                \
    X(lists)                                                                   \
    X(files)                                                                   \
    X(verify_sumint)                                                           \
    X(to_verify)                                                               \
    X(extra_args)                                                              \
    X(use_algo)                                                                \
    X(to_compare)                                                              \
    X(canonical)                                                               \
    X(algo_to_info)

namespace cmdline_group
{
#define CMDGROUP_GEN(name) inline constexpr const char *name = #name;
CMDGROUP_LIST(CMDGROUP_GEN)
#undef CMDGROUP_GEN
} // namespace cmdline_group

namespace cmdline_entry
{
#define CMDENTRY_GEN(name) inline constexpr const char *name = #name;
CMDENTRY_LIST(CMDENTRY_GEN)
#undef CMDENTRY_GEN
} // namespace cmdline_entry

namespace subcommands
{
#define SUBCOMMANDS_STRINGS(name) inline constexpr const char *name = #name;
SUBCOMMAND_LIST(SUBCOMMANDS_STRINGS)
#undef SUBCOMMANDS_STRINGS
} // namespace subcommands

using namespace argparser;

static inline void error_prompt(std::ostream &os, ap_instance &cmdline,
                                bool ignore_redundant, bool hint = true)
{
    for (auto &err : cmdline.errors())
    {
        // A redundant argument carries the raw token, not an entry name, so it
        // cannot go through entry(): either report it as it is, or drop it
        // because the caller asked for redundancy to be ignored.
        if (err.second == argparser::ap_result::invalid_arguments)
        {
            if (!ignore_redundant)
                os << "Error: invalid argument: " << err.first << std::endl;
            continue;
        }
        os << "Error: " << argparser::describe(err.second) << ": "
           << [&cmdline, &err]()
        {
            auto &entry = cmdline.entry(err.first);
            if (entry.explanations.empty())
                return entry.name;
            return entry.explanations.at(0);
        }() << std::endl;
    }
    if (hint)
        os << "Try '"
           << std::filesystem::path(cmdline.argv_zero()).filename().string()
           << " --help' for more information." << std::endl;
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
                cmdline_entry::list_alg,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::general},
                    {ap_defkey::explanations,"-a or --algorithm"},
                    {ap_defkey::long_name,"algorithm"},
                    {ap_defkey::short_name,"a"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry(
                cmdline_entry::comp,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::general},
                    {ap_defkey::explanations,"-C or --completions"},
                    {ap_defkey::long_name,"completions"},
                    {ap_defkey::short_name,"C"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp, src/verify_core.cpp
                cmdline_entry::key_file,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::explanations,"-K or --key-file"},
                    {ap_defkey::long_name,"key-file"},
                    {ap_defkey::short_name,"K"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry(
                cmdline_entry::text,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::explanations,"-T or --text"},
                    {ap_defkey::long_name,"text"},
                    {ap_defkey::short_name,"T"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp, src/verify_core.cpp
                cmdline_entry::key,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::explanations,"-k or --key"},
                    {ap_defkey::long_name,"key"},
                    {ap_defkey::short_name,"k"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp, src/verify_core.cpp
                cmdline_entry::threads,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::explanations,"-t or --thread"},
                    {ap_defkey::long_name,"thread"},
                    {ap_defkey::short_name,"t"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp, src/verify_core.cpp, src/cmp_core.cpp
                cmdline_entry::output,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::group, cmdline_group::cmp},
                    {ap_defkey::explanations,"-o or --output"},
                    {ap_defkey::long_name,"output"},
                    {ap_defkey::short_name,"o"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp, src/verify_core.cpp
                cmdline_entry::verbose,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::sum},
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::explanations,"-v or --verbose"},
                    {ap_defkey::long_name,"verbose"},
                    {ap_defkey::short_name,"v"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::excludes,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum_greedy},
                    {ap_defkey::explanations,"-e or --exclude"},
                    {ap_defkey::long_name,"exclude"},
                    {ap_defkey::short_name,"e"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::lists,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::sum_greedy},
                    {ap_defkey::explanations,"-l or --list"},
                    {ap_defkey::long_name,"list"},
                    {ap_defkey::short_name,"l"},
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
                    {ap_defkey::group, cmdline_group::sum_greedy},
                    {ap_defkey::explanations,"files or directories"},
                }
            ),
            ap_entry( // Handled in src/sum_core.cpp
                cmdline_entry::verify_sumint,
                ap_type::flag,
                { // This is the internal option, which should not be exposed to the user.
                    {ap_defkey::group,cmdline_group::sum_int},
                    {ap_defkey::explanations,"--verify"},
                    {ap_defkey::long_name,"verify"},
                    {ap_defkey::max_repetition,"1"}
                }
            ),
            ap_entry(
                cmdline_entry::use_algo,
                ap_type::option,
                {
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::explanations,"-u or --use-algo"},
                    {ap_defkey::long_name,"use-algo"},
                    {ap_defkey::short_name,"u"},
                    {ap_defkey::max_value_count,"1"},
                }
            ),
            ap_entry( // Handled in src/verify_core.cpp
                cmdline_entry::to_verify,
                ap_type::positional,
                {
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::explanations,"checksum result to verify"},
                    {ap_defkey::min_positional_count,"1"},
                    {ap_defkey::max_positional_count,"1"},
                }
            ),
            ap_entry(
                cmdline_entry::extra_args,
                ap_type::positional,
                {
                    {ap_defkey::group, cmdline_group::verify},
                    {ap_defkey::explanations,"more arguments for checksum"},
                }
            ),
            ap_entry( // Handled in src/cmp_core.cpp
                cmdline_entry::canonical,
                ap_type::flag,
                {
                    {ap_defkey::group, cmdline_group::cmp},
                    {ap_defkey::explanations,"-c or --canonical"},
                    {ap_defkey::long_name,"canonical"},
                    {ap_defkey::short_name,"c"},
                    {ap_defkey::max_repetition,"1"},
                }
            ),
            ap_entry( // Handled in src/cmp_core.cpp
                cmdline_entry::to_compare,
                ap_type::positional,
                {
                    {ap_defkey::group, cmdline_group::cmp},
                    {ap_defkey::explanations,"the two file lists to compare"},
                    {ap_defkey::min_positional_count,"2"},
                    {ap_defkey::max_positional_count,"2"},
                }
            ),
            ap_entry( // Handled in src/algo_info_core.cpp
                cmdline_entry::algo_to_info,
                ap_type::positional,
                {
                    {ap_defkey::group, cmdline_group::algo_info},
                    {ap_defkey::explanations,"the algorithm to inquire"},
                    {ap_defkey::min_positional_count,"1"},
                    {ap_defkey::max_positional_count,"1"},
                }
            )
        }
    );
    //clang-format on
}
}
#endif
