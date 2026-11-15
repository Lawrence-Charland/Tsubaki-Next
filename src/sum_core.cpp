// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "argparser.hpp"
#include "basic_taskstream.hpp"
#include "cached_taskstream.hpp"
#include "checksum.hpp"
#include "cmdline.hpp"
#include "core_functions.hpp"
#include "dirscanner.hpp"
#include "exit_codes.hpp"
#include "fanin_taskstream.hpp"
#include "listscanner.hpp"
#include "worker.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace tsubaki
{
int sum_main(argparser::ap_instance &cmdline)
{
    using namespace std::filesystem;

    cmdline.evaluate(cmdline_group::sum);

    const std::string program = path(cmdline.argv_zero()).filename().string();

    // Check errors (early stage)
    if (!cmdline.fine(false))
    {
        error_prompt(std::cerr, cmdline);
        return TSUBAKI_INVALID_ARGUMENT;
    }

    // Basic values
    fanin_taskstream tasks;
    algorithm alg = algorithm::error;
    unsigned threads = 0;
    std::queue<path> files;
    std::vector<std::unique_ptr<std::ifstream>> lists;
    std::vector<path> excludes;
    bool follow_symlinks = static_cast<bool>(
             cmdline.entry(cmdline_entry::follow_symlinks).repetition),
         force_rescan = static_cast<bool>(
             cmdline.entry(cmdline_entry::force_rescan).repetition),
         verbose = static_cast<bool>(
             cmdline.entry(cmdline_entry::verbose).repetition);
    std::ostream *out;
    std::ofstream fout;

    // Check and store arguments
    {
        // Decide the algorithm
        {
            auto &content = cmdline.entry(cmdline_entry::algorithm).content;
            alg = str_to_alg(content.at(0));
            if (alg == algorithm::error)
            {
                std::cerr << "Error: unknown algorithm: \"" << content.at(0)
                          << "\"." << std::endl;
                return TSUBAKI_INVALID_ARGUMENT;
            }
        }
        // Decide the thread count
        {
            int thds;
            auto &content = cmdline.entry(cmdline_entry::threads).content;
            if (!content.empty())
            {
                try
                {
                    thds = std::stoi(content.at(0));
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error: \"" << content.at(0)
                              << "\" is not a valid integer." << std::endl;
                    return TSUBAKI_INVALID_ARGUMENT;
                }
                if (thds <= 0)
                {
                    std::cerr << "Error: thread count must be greater than "
                                 "zero."
                              << std::endl;
                    return TSUBAKI_INVALID_ARGUMENT;
                }
                threads = static_cast<unsigned>(thds);
            }
            else
            {
                thds = std::thread::hardware_concurrency();
                if (thds == 0)
                {
                    std::cerr << "Warning: std::thread::hardware_concurrency() "
                                 "returned 0; falling back to a single worker "
                                 "thread."
                              << std::endl;
                    threads = 1;
                }
                else
                    threads = thds;
            }
        }
        // Decide where to output
        {
            auto &content = cmdline.entry(cmdline_entry::output).content;
            if (content.empty())
                out = &std::cout;
            else
            {
                fout.open(content.at(0));
                if (!fout.good())
                {
                    std::cerr << "Error: cannot open \"" + content.at(0) +
                                     "\" for writing."
                              << std::endl;
                    return TSUBAKI_FILE_WRITE_FAILED;
                }
                out = &fout;
            }
        }
        // Store the excludes
        for (auto &it : cmdline.entry(cmdline_entry::excludes).content)
            excludes.emplace_back(it);
        // Parse the files ( - stands for stdin )
        for (auto &it : cmdline.entry(cmdline_entry::files).content)
        {
            if (it ==
                "-") // Add a listscanner reads stdin ( safe to multiply add )
                tasks.add_task(std::make_unique<listscanner>(
                    "stdin", std::cin, *out, force_rescan));
            else
            {
                std::error_code ec;
                path target(it);
                if (is_directory(target, ec)) // Add a dirscanner
                    tasks.add_task(std::make_unique<dirscanner>(
                        target, excludes, follow_symlinks));
                else
                    files.push(target); // Add single files
            }
        }
        tasks.add_task(std::make_unique<cached_taskstream>(files));
        // Get and check the textual file lists
        for (auto &it : cmdline.entry(cmdline_entry::lists).content)
        {
            lists.emplace_back(std::make_unique<std::ifstream>(it));
            if (!lists.back()->good())
                tasks.report_error(error_entry(
                    std::make_error_code(std::errc::no_such_file_or_directory),
                    "opening file list " + it));
            else // Add a listscanner reads ifstream
                tasks.add_task(std::make_unique<listscanner>(
                    it, *lists.back(), *out, force_rescan));
        }
    }

    // Run work
    work_result result = work_manager(tasks, *out, alg, verbose).run(threads);
    *out << "#\n";
    result.print(*out, true);
    if (out != &std::cout)
        result.print(std::clog, false);
    if (result.interrupted)
        return TSUBAKI_INTERRUPTED;
    if (!result.errors.empty())
        return TSUBAKI_HASH_ERROR;
    return TSUBAKI_SUCCESS;
}
} // namespace tsubaki