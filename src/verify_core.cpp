// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "argparser.hpp"
#include "checksum.hpp"
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
        error_prompt(std::cerr, cmdline);
        return TSUBAKI_INVALID_OPERATION;
    }

    std::string input = cmdline.entry(cmdline_entry::to_verify).content.at(0);
    std::string output;
    algorithm algo;

    {
        std::error_code ec;
        if (!is_regular_file(path(input), ec))
        {
            std::cerr << "Error: cannot read the input file \"" << input
                      << "\"." << std::endl;
            return TSUBAKI_FILE_READ_FAILED;
        }
        auto &content = cmdline.entry(cmdline_entry::use_algo).content;
        if (!content.empty())
        {
            algo = str_to_alg(content.at(0));
            if (algo == algorithm::error)
            {
                std::cerr << "Error: invalid algorithm: \"" << content.at(0)
                          << "\"." << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
            std::cerr << "Forced to use algorithm " << content.at(0) << "."
                      << std::endl;
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
            algo = str_to_alg(get_algo.value());
            if (algo == algorithm::error)
            {
                std::cerr << "Error: unknown algorithm: \"" << get_algo.value()
                          << "\"." << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
            if (algo == algorithm::none)
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
        if (content.empty())
        {
            output = path(input).concat(".verify").string();
            std::cerr << "The output file will be \"" << output << "\"."
                      << std::endl;
        }
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
    sum_cmd.push_back(alg_to_str(algo));
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
    sum_cmd.push_back("--force-rescan");
    sum_cmd.push_back("--verify");

    ap_instance sum_cmd_ap = def_args();
    sum_cmd_ap.pre_parse(sum_cmd);
    return sum_main(sum_cmd_ap, true);
}
} // namespace tsubaki