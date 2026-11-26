// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "argparser.hpp"
#include "checksum_api.hpp"
#include "cmdline.hpp"
#include "core_functions.hpp"
#include "exit_codes.hpp"
#include "utilities.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace tsubaki
{
int verify_main(argparser::ap_instance &cmdline)
{
    using namespace std::filesystem;

    cmdline.evaluate(cmdline_group::verify);
    if (!cmdline.fine(false))
    {
        error_prompt(std::cerr, cmdline, false);
        return TSUBAKI_INVALID_OPERATION;
    }

    std::string input = cmdline.entry(cmdline_entry::to_verify).content.at(0);
    std::string output;
    checksum_api resolved;

    {
        std::error_code ec;
        if (input != "-" &&
            (!exists(path(input), ec) || is_directory(path(input), ec)))
        {
            std::cerr << "Error: cannot read the input file \"" << input
                      << "\"." << std::endl;
            return TSUBAKI_FILE_READ_FAILED;
        }
        auto &content = cmdline.entry(cmdline_entry::use_algo).content;
        if (!content.empty())
        {
            resolved.set_algo(content.at(0));
            if (!resolved.ok())
            {
                std::cerr << "Error: invalid algorithm: \"" << content.at(0)
                          << "\"." << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
            std::cerr << "Forced to use algorithm " << content.at(0) << "."
                      << std::endl;
        }
        else if (input == "-")
        {
            std::cerr << "Error: cannot prefetch the \"Algorithm:\" header "
                         "from stdin.\nPlease use -u to specify the algorithm."
                      << std::endl;
            return TSUBAKI_INVALID_OPERATION;
        }
        else
        {
            auto get_algo = get_comment_value(path(input), "Algorithm");
            if (!get_algo)
            {
                std::cerr << "Error: no \"Algorithm:\" header in \"" << input
                          << "\"." << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
            resolved.set_algo(get_algo.value());
            if (!resolved.ok())
            {
                std::cerr << "Error: unknown algorithm: \"" << get_algo.value()
                          << "\"." << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
            if (!resolved.hashing())
            {
                std::cerr
                    << "Error: a listing made with algorithm \"none\" has "
                       "no checksums to verify."
                    << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
        }
    }
    {
        auto &content = cmdline.entry(cmdline_entry::output).content;
        if (content.empty() || content.at(0) == "-")
            output = "-";
        else
            output = content.at(0);
    }
    if (output != "-")
    {
        std::error_code ec;
        if (equivalent(path(input), path(output), ec))
        {
            std::cerr << "Error: the output file must not be the input file."
                      << std::endl;
            return TSUBAKI_INVALID_OPERATION;
        }
    }

    std::vector<std::string> sum_cmd = {std::string(cmdline.argv_zero())};
    sum_cmd.push_back(resolved.name());
    sum_cmd.push_back("-l");
    sum_cmd.push_back(input);
    {
        auto &content = cmdline.entry(cmdline_entry::threads).content;
        if (!content.empty())
        {
            sum_cmd.push_back("-t");
            sum_cmd.insert(sum_cmd.end(), content.begin(), content.end());
        }
    }
    {
        auto &content = cmdline.entry(cmdline_entry::key).content;
        if (!content.empty())
        {
            sum_cmd.push_back("-k");
            sum_cmd.insert(sum_cmd.end(), content.begin(), content.end());
        }
    }
    {
        auto &content = cmdline.entry(cmdline_entry::key_file).content;
        if (!content.empty())
        {
            sum_cmd.push_back("-K");
            sum_cmd.insert(sum_cmd.end(), content.begin(), content.end());
        }
    }
    if (output != "-")
    {
        sum_cmd.push_back("-o");
        sum_cmd.push_back(output);
    }
    if (cmdline.entry(cmdline_entry::verbose).repetition)
        sum_cmd.push_back("-v");
    if (cmdline.entry(cmdline_entry::text).repetition)
        sum_cmd.push_back("--text");
    sum_cmd.push_back("--force-rescan");
    sum_cmd.push_back("--verify");
    {
        auto &content = cmdline.entry(cmdline_entry::extra_args).content;
        sum_cmd.insert(sum_cmd.end(), content.begin(), content.end());
    }

    ap_instance sum_cmd_ap = def_args();
    sum_cmd_ap.pre_parse(sum_cmd);
    return sum_main(sum_cmd_ap, true);
}
} // namespace tsubaki