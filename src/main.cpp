// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "argparser.hpp"
#include "cmdline.hpp"
#include "core_functions.hpp"
#include "exit_codes.hpp"
#include "help.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    try
    {
        if (argc == 0)
        {
            std::cerr << "Error: argc is 0, refuses to run." << std::endl;
            return TSUBAKI_FATAL;
        }
        std::vector<std::string> args(argv, argv + argc);
        argparser::ap_instance cmdline = tsubaki::def_args();
        cmdline.pre_parse(args);
        cmdline.evaluate(tsubaki::cmdline_group::general);
        const std::string program =
            std::filesystem::path(cmdline.argv_zero()).filename().string();
        if (cmdline.entry(tsubaki::cmdline_entry::help).repetition)
        {
            std::cout << tsubaki::help_text(program) << std::endl;
            return TSUBAKI_SUCCESS;
        }
        if (cmdline.entry(tsubaki::cmdline_entry::version).repetition)
        {
            std::cout << program << " " << TSUBAKI_VERSION << std::endl;
            return TSUBAKI_SUCCESS;
        }
        if (!cmdline.fine())
        {
            tsubaki::error_prompt(std::cerr, cmdline);
            return TSUBAKI_INVALID_ARGUMENT;
        }
        const std::string subcommand =
            cmdline.entry(tsubaki::cmdline_entry::subcmd).content.at(0);
        if (subcommand == tsubaki::subcommands::sum)
            return tsubaki::sum_main(cmdline);
        else
        {
            std::cerr << "Error: unknown subcommand \"" << subcommand << "\""
                      << std::endl;
            std::cerr << "Try '" << program << " --help' for more information."
                      << std::endl;
            return TSUBAKI_INVALID_ARGUMENT;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return TSUBAKI_UNEXPECTED;
    }
}
