// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "algo_info_core.hpp"
#include "argparser.hpp"
#include "cmdline.hpp"
#include "core_functions.hpp"
#include "exit_codes.hpp"
#include "help.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int tsubaki::tsubaki_entrance(const std::vector<std::string> &args)
{
    using namespace tsubaki;

    // Early argument check
    if (args.empty())
    {
        std::cerr << "Error: argc is 0, refuses to run." << std::endl;
        return TSUBAKI_FATAL;
    }
    argparser::ap_instance cmdline = def_args();
    cmdline.pre_parse(args);
    cmdline.evaluate(cmdline_group::general);
    const std::string program =
        std::filesystem::path(cmdline.argv_zero()).filename().string();

    // Help, version and list args. Shouldn't check errors.
    if (cmdline.entry(cmdline_entry::help).repetition)
    {
        print_help(std::cout, program);
        return TSUBAKI_SUCCESS;
    }
    if (cmdline.entry(cmdline_entry::version).repetition)
    {
        show_version(std::cout);
        return TSUBAKI_SUCCESS;
    }
    if (cmdline.entry(cmdline_entry::list_alg).repetition)
    {
        list_algorithm(std::cout);
        return TSUBAKI_SUCCESS;
    }
    if (cmdline.entry(cmdline_entry::comp).repetition)
    {
        print_completion(std::cout);
        return TSUBAKI_SUCCESS;
    }

    // Check error
    if (!cmdline.fine())
    {
        error_prompt(std::cerr, cmdline, true);
        return TSUBAKI_INVALID_OPERATION;
    }

    // Assign to subcommands
    const std::string subcommand =
        cmdline.entry(cmdline_entry::subcmd).content.at(0);
    if (subcommand == subcommands::sum)
        return sum_main(cmdline);
    else if (subcommand == subcommands::verify)
        return verify_main(cmdline);
    else if (subcommand == subcommands::cmp)
        return cmp_main(cmdline);
    else if (subcommand == subcommands::algo)
        return algo_info_core(cmdline);
    else
    {
        std::cerr << "Error: unknown subcommand \"" << subcommand << "\""
                  << std::endl;
        std::cerr << "Try '" << program << " --help' for more information."
                  << std::endl;
        return TSUBAKI_INVALID_OPERATION;
    }
}