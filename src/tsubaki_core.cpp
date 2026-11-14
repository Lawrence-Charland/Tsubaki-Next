// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "tsubaki_core.hpp"
#include "argparser.hpp"
#include "basic_taskstream.hpp"
#include "cached_taskstream.hpp"
#include "checksum.hpp"
#include "cmdline.hpp"
#include "dirscanner.hpp"
#include "fanin_taskstream.hpp"
#include "help.hpp"
#include "listscanner.hpp"
#include "worker.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace tsubaki
{
int tsubaki_main(int argc, char **argv)
{
    using namespace std::filesystem;

    // Basic argument parse
    argparser::ap_instance cmdline = def_args();
    cmdline.pre_parse(argc, argv);
    cmdline.evaluate();

    const std::string program = path(cmdline.argv_zero()).filename().string();

    // Print the help message first ( it does not need the other arguments )
    if (cmdline.entry("help").repetition)
    {
        std::cout << help_text(program);
        std::cout.flush();
        return 0;
    }

    if (cmdline.entry("version").repetition)
    {
        std::cout << "tsubaki " << version << std::endl;
        return 0;
    }

    // Check errors (early stage)
    if (!cmdline.fine(false))
    {
        for (auto &err : cmdline.errors())
        {
            if (err.second == argparser::ap_result::invalid_arguments)
                std::cerr << "Error: invalid argument: " << err.first
                          << std::endl;
            else
                std::cerr << "Error: " << argparser::describe(err.second)
                          << ": " << cmdline.entry(err.first).explanations.at(1)
                          << std::endl;
        }
        std::cerr << "Try '" << program << " --help' for more information."
                  << std::endl;
        return 1;
    }

    // Basic values
    fanin_taskstream tasks;
    algorithm alg = algorithm::error;
    unsigned threads = 0;
    std::vector<path> files;
    std::vector<std::unique_ptr<std::ifstream>> lists;
    std::vector<path> excludes;
    bool follow_symlinks =
             static_cast<bool>(cmdline.entry("follow-symlinks").repetition),
         force_rescan =
             static_cast<bool>(cmdline.entry("force-rescan").repetition),
         verbose = static_cast<bool>(cmdline.entry("verbose").repetition);
    std::ostream *out;
    std::ofstream fout;

    // Check and store arguments
    {
        // Decide the algorithm
        {
            auto &content = cmdline.entry("algorithm").content;
            alg = str_to_alg(content.at(0));
            if (alg == algorithm::error)
            {
                std::cerr << "Error: unknown algorithm: \"" << content.at(0)
                          << "\"." << std::endl;
                return 1;
            }
        }
        // Decide the thread count
        {
            int thds;
            auto &content = cmdline.entry("threads").content;
            if (!content.empty())
            {
                try
                {
                    thds = std::stoi(content.at(0));
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error: failed to convert \"" << content.at(0)
                              << "\" to integer." << std::endl;
                    return 1;
                }
                if (thds <= 0)
                {
                    std::cerr << "Error: thread count must be > 0."
                              << std::endl;
                    return 1;
                }
                threads = static_cast<unsigned>(thds);
            }
            else
            {
                thds = std::thread::hardware_concurrency();
                if (thds == 0)
                {
                    std::cerr << "Warning: std::thread::hardware_concurrency() "
                                 "returns "
                                 "0, will fall back to single worker thread."
                              << std::endl;
                    threads = 1;
                }
                else
                    threads = thds;
            }
        }
        // Decide where to output
        {
            auto &content = cmdline.entry("output").content;
            if (content.empty())
                out = &std::cout;
            else
            {
                fout.open(content.at(0));
                if (!fout.good())
                {
                    std::cerr << "Error: cannot write to file " + content.at(0)
                              << std::endl;
                    return 1;
                }
                out = &fout;
            }
        }
        // Store the excludes
        for (auto &it : cmdline.entry("excludes").content)
            excludes.emplace_back(it);
        // Parse the files ( - stands for stdin )
        for (auto &it : cmdline.entry("files").content)
        {
            if (it ==
                "-") // Add a listscanner reads stdin ( safe to multiply add )
                tasks.add_task(std::make_unique<listscanner>(std::cin, *out,
                                                             force_rescan));
            else
            {
                std::error_code ec;
                path target(it);
                if (is_directory(target, ec)) // Add a dirscanner
                    tasks.add_task(std::make_unique<dirscanner>(
                        target, excludes, follow_symlinks));
                else
                    files.push_back(target); // Add single files
            }
        }
        tasks.add_task(std::make_unique<cached_taskstream>(files));
        // Get and check the textual file lists
        for (auto &it : cmdline.entry("lists").content)
        {
            lists.emplace_back(std::make_unique<std::ifstream>(it));
            if (!lists.back()->good())
                tasks.report_error(error_entry(
                    std::make_error_code(std::errc::no_such_file_or_directory),
                    "while trying to open file list " + it));
            else // Add a listscanner reads ifstream
                tasks.add_task(std::make_unique<listscanner>(
                    *lists.back(), *out, force_rescan));
        }
    }

    // Run work
    work_result result = work_manager(tasks, *out, alg, verbose).run(threads);
    *out << "#\n" << result;
    if (out != &std::cout)
        std::clog << result;
    return result.errors.empty() ? 0 : 1;
}
} // namespace tsubaki