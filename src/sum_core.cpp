// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "argparser.hpp"
#include "basic_taskstream.hpp"
#include "cached_taskstream.hpp"
#include "checksum_api.hpp"
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
#include <utility>
#include <vector>

namespace tsubaki
{
int sum_main(argparser::ap_instance &cmdline, bool internal)
{
    using namespace std::filesystem;

    cmdline.evaluate(cmdline_group::sum);
    if (internal)
        cmdline.evaluate(cmdline_group::sum_int);

    const std::string program = path(cmdline.argv_zero()).filename().string();

    // Check errors (early stage)
    if (!cmdline.fine())
    {
        error_prompt(std::cerr, cmdline, true);
        return TSUBAKI_INVALID_OPERATION;
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
         verify = static_cast<bool>(
             cmdline.entry(cmdline_entry::verify_sumint).repetition),
         key_from_stdin = false;
    std::ostream *out;
    std::ofstream fout;

    // Check and store arguments
    {
        // Decide the algorithm
        {
            auto &content = cmdline.entry(cmdline_entry::algorithm).content;
            api.set_algo(content.at(0));
            if (!api.ok())
            {
                std::cerr << "Error: unknown algorithm: \"" << content.at(0)
                          << "\"." << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
            try
            {
                auto chk_args = api.check_args(cmdline);
                if (chk_args)
                {
                    std::cerr << chk_args.value() << std::endl;
                    std::cerr << "The argument requirement belongs to \""
                              << api.name() << "\". You can try " << program
                              << " algo-info " << api.name() << std::endl;
                    return TSUBAKI_INVALID_OPERATION;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error: failed to check the arguments required by "
                             "the dynamic provider.\n what():\n"
                          << e.what() << std::endl;
                return TSUBAKI_FATAL;
            }
        }
        // Decide the key
        {
            auto &key_cont = cmdline.entry(cmdline_entry::key).content;
            auto &key_file_cont =
                cmdline.entry(cmdline_entry::key_file).content;
            if (!key_cont.empty() && !key_file_cont.empty())
            {
                std::cerr << "Error: specifying key and key file at the same "
                             "time is not allowed."
                          << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
            if (!api.keyed())
            {
                if (!key_cont.empty() || !key_file_cont.empty() ||
                    cmdline.entry(cmdline_entry::text).repetition)
                {
                    std::cerr << "Warning: \"" << api.name()
                              << "\" doesn't need key." << std::endl;
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
                    auto key_read = get_file_content(
                        *is_key,
                        static_cast<bool>(
                            cmdline.entry(cmdline_entry::text).repetition));
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
                if (!api.set_key(std::move(key)))
                {
                    if (api.error() == checksum_error::missing_key)
                        std::cerr << "Error: \"" << api.name()
                                  << "\" needs a key." << std::endl;
                    else
                        std::cerr
                            << "Error: the key is not valid for algorithm \""
                            << api.name() << "\"." << std::endl;
                    return TSUBAKI_INVALID_OPERATION;
                }
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
                    return TSUBAKI_INVALID_OPERATION;
                }
                if (thds <= 0)
                {
                    std::cerr << "Error: thread count must be greater than "
                                 "zero."
                              << std::endl;
                    return TSUBAKI_INVALID_OPERATION;
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
            if (content.empty() || content.at(0) == "-")
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
        // Parse the greedy arguments
        cmdline.evaluate(cmdline_group::sum_greedy);
        if (!cmdline.fine(false))
        {
            error_prompt(std::cerr, cmdline, false);
            return TSUBAKI_INVALID_OPERATION;
        }
        // Store the excludes
        for (auto &it : cmdline.entry(cmdline_entry::excludes).content)
            excludes.emplace_back(it);
        // Parse the files ( - stands for stdin )
        for (auto &it : cmdline.entry(cmdline_entry::files).content)
        {
            if (it == "-")
            {
                // stdin is already the key source; naming it again is invalid.
                if (key_from_stdin)
                {
                    std::cerr << "Error: stdin was used for the key."
                              << std::endl;
                    return TSUBAKI_INVALID_OPERATION;
                }
                // Add a listscanner reads stdin ( safe to multiply add )
                tasks.add_task(std::make_unique<listscanner>(
                    "stdin", std::cin, *out, force_rescan, verify));
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
            if (it == "-") // "-l -" is also acceptable
            {
                // stdin is already the key source; naming it again is invalid.
                if (key_from_stdin)
                {
                    std::cerr << "Error: stdin was used for the key."
                              << std::endl;
                    return TSUBAKI_INVALID_OPERATION;
                }
                // Add a listscanner reads stdin ( safe to multiply add )
                tasks.add_task(std::make_unique<listscanner>(
                    "stdin", std::cin, *out, force_rescan, verify));
            }
            else
            {
                lists.emplace_back(std::make_unique<std::ifstream>(it));
                if (!lists.back()->good())
                    tasks.report_error(
                        error_entry(std::make_error_code(
                                        std::errc::no_such_file_or_directory),
                                    "opening file list " + it));
                else // Add a listscanner reads ifstream
                    tasks.add_task(std::make_unique<listscanner>(
                        it, *lists.back(), *out, force_rescan, verify));
            }
        }
    }

    // Run work
    work_result result =
        work_manager(tasks, *out, api, verify, verbose, true).run(threads);
    *out << "#\n";
    result.print(*out, true);
    if (out != &std::cout)
        result.print(std::clog, false);

    // Return
    out->flush();
    if (out->fail())
    {
        std::cerr << "Error: failed to write output; it may be incomplete."
                  << std::endl;
        return TSUBAKI_FILE_WRITE_FAILED;
    }
    if (result.interrupted)
        return TSUBAKI_INTERRUPTED;
    if (!result.errors.empty())
        return TSUBAKI_HASH_ERROR;
    return TSUBAKI_SUCCESS;
}
} // namespace tsubaki