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
#include "utilities.hpp"
#include "worker.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
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
    checksum_api api;
    unsigned threads = 0;
    std::queue<path> files;
    std::vector<std::unique_ptr<std::ifstream>> lists;
    std::vector<path> excludes;
    bool follow_symlinks = static_cast<bool>(
             cmdline.entry(cmdline_entry::follow_symlinks).repetition),
         force_rescan = static_cast<bool>(
             cmdline.entry(cmdline_entry::force_rescan).repetition),
         verbose = static_cast<bool>(
             cmdline.entry(cmdline_entry::verbose).repetition),
         key_from_stdin = false;
    std::ostream *out;
    std::ofstream fout;

    // Check and store arguments
    {
        // Decide the algorithm
        {
            auto &content = cmdline.entry(cmdline_entry::algorithm).content;
            api.alg = str_to_alg(content.at(0));
            if (api.alg == algorithm::error)
            {
                std::cerr << "Error: unknown algorithm: \"" << content.at(0)
                          << "\"." << std::endl;
                return TSUBAKI_INVALID_ARGUMENT;
            }
        }
        // Check the key
        {
            auto &key_cont = cmdline.entry(cmdline_entry::key).content;
            auto &key_file_cont =
                cmdline.entry(cmdline_entry::key_file).content;
            if (!key_cont.empty() && !key_file_cont.empty())
            {
                std::cerr << "Error: specifying key and key file at the same "
                             "time is not allowed."
                          << std::endl;
                return TSUBAKI_INVALID_ARGUMENT;
            }
            if (!is_keyed(api.alg))
            {
                // The key is irrelevant, so never open the file: that keeps
                // "-K -" from stealing stdin when a list also wants it.
                if (!key_cont.empty() || !key_file_cont.empty())
                {
                    std::cerr << "Warning: \"" << alg_to_str(api.alg)
                              << "\" doesn't need key." << std::endl;
                    std::cerr << "Will go ahead anyway ignoring the specified "
                                 "key."
                              << std::endl;
                }
            }
            else
            {
                std::string key;
                if (!key_cont.empty())
                    key = key_cont.at(0);
                else if (!key_file_cont.empty())
                {
                    const bool from_stdin = key_file_cont.at(0) == "-";
                    key_from_stdin = from_stdin;
                    std::ifstream ifs_key_file;
                    std::istream *is_key = &std::cin;
                    if (!from_stdin)
                    {
                        ifs_key_file.open(key_file_cont.at(0));
                        is_key = &ifs_key_file;
                    }
                    auto key_read = get_key(*is_key);
                    if (!key_read)
                    {
                        std::cerr
                            << "Error: cannot read a key from "
                            << (from_stdin ? "stdin"
                                           : "\"" + key_file_cont.at(0) + "\"")
                            << "." << std::endl;
                        return TSUBAKI_FILE_READ_FAILED;
                    }
                    key = key_read.value();
                }
                if (key.empty())
                {
                    std::cerr << "Error: \"" << alg_to_str(api.alg)
                              << "\" needs a key." << std::endl;
                    return TSUBAKI_INVALID_ARGUMENT;
                }
                if (!check_key_validity(key, api.alg))
                {
                    std::cerr << "Error: the key for \"" << alg_to_str(api.alg)
                              << "\" must be at most "
                              << max_key_length(api.alg) << " bytes."
                              << std::endl;
                    return TSUBAKI_INVALID_ARGUMENT;
                }
                api.key = key;
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
            if (it == "-")
            {
                // stdin is already the key source; naming it again as an
                // input would read the same stream twice.
                if (key_from_stdin)
                {
                    std::cerr << "Error: stdin was used for the key."
                              << std::endl;
                    return TSUBAKI_INVALID_ARGUMENT;
                }
                // Add a listscanner reads stdin ( safe to multiply add )
                tasks.add_task(std::make_unique<listscanner>(
                    "stdin", std::cin, *out, force_rescan));
            }
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
    work_result result = work_manager(tasks, *out, api, verbose).run(threads);
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