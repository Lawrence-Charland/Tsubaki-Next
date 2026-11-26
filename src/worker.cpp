// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "worker.hpp"
#include "basic_taskstream.hpp"
#include "checksum.hpp"
#include "checksum_api.hpp"
#include "exit_codes.hpp"
#include "utilities.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <syncstream>
#include <thread>
#include <unistd.h>
#include <vector>

namespace tsubaki
{
void work_result::print(std::ostream &os, bool in_details)
{
    os << "# ---------- Summary ----------\n";
    os << "# Algorithm:         " << algorithm_name << std::endl;
    os << "# Processed:         " << success + failed << std::endl;
    os << "# Hashed:            " << success << std::endl;
    os << "# Failed:            " << failed << std::endl;
    os << "# Not hashed:        " << unprocessed << std::endl;
    os << "# Duration:          "
       << std::chrono::duration<double>(duration).count() << " seconds"
       << std::endl;
    if (verify)
    {
        os << "# Changed:           " << changed.size() << std::endl;
        if (in_details)
        {
            if (!changed.empty())
            {
                os << "#\n# Changed files:" << std::endl;
                for (auto &files : changed)
                    os << "# " << shown_name(files.p.string()) << std::endl;
                os << "#\n";
            }
        }
        os << "# Unsure:            " << unsure.size() << std::endl;
        if (in_details)
        {
            if (!unsure.empty())
            {
                os << "#\n# Unsure files:" << std::endl;
                for (auto &files : unsure)
                    os << "# " << shown_name(files.p.string()) << std::endl;
                os << "#\n";
            }
        }
    }
    os << "# Errors:            " << errors.size() << std::endl;
    if (interrupted)
        os << "#\n# Warning: The process was interrupted halfway through."
           << std::endl;
    if (!in_details || errors.empty())
        return;
    os << "#\n# Error messages:" << std::endl;
    for (auto &it : errors)
    {
        os << "# Value: " << it.ec.value() << ", Message: " << it.ec.message()
           << ", Operation: " << shown_name(it.op) << std::endl;
    }
}

namespace
{
std::atomic<bool> *volatile g_interrupt = nullptr;

std::atomic<unsigned> volatile int_count = 0;

// write_line() takes the digest as a string; the literal is a C string.
const std::string none_sum(none_checksum);

const constexpr char *term_msg =
    "\nTerminated after Ctrl+C was pressed 5 times.\n";

const constexpr char *int_msg =
    "Completing the output manifest, please wait...";

// is_checksum() accepts uppercase input, so a listing written by another tool
// must not look changed merely because its hex digits are uppercase.
bool same_digest(const std::string &lhs, const std::string &rhs)
{
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](char l, char r)
                      {
                          return std::tolower(static_cast<unsigned char>(l)) ==
                                 std::tolower(static_cast<unsigned char>(r));
                      });
}

void on_interrupt(int)
{
    int_count++;
    if (g_interrupt)
        g_interrupt->store(true, std::memory_order_relaxed);
    if (int_count == 5)
    {
        write(2, term_msg, strlen(term_msg));
        _Exit(TSUBAKI_TERMINATED);
    }
}
} // namespace

void work_manager::worker(bool verify)
{
    std::optional<task_token> target;
    std::vector<task_token> _changed;
    std::vector<task_token> _unsure;
    while ((target = tasks.pop(), target))
    {
        if (interrupted.load(std::memory_order_relaxed))
        {
            std::osyncstream out(os);
            write_line(none_sum, target->p, out);
            unprocessed.fetch_add(1, std::memory_order_relaxed);
            if (verify)
                _unsure.push_back(target.value());
            continue;
        }

        auto checksum = api.calc(target->p);
        if (!checksum)
        {
            tasks.report_error(
                error_entry(checksum.error(), "calculating the checksum of " +
                                                  target->p.string()));
            std::osyncstream out(os);
            write_line(none_sum, target->p, out);
            failed.fetch_add(1, std::memory_order_relaxed);
            if (verify)
                _unsure.push_back(target.value());
        }
        else
        {
            std::osyncstream out(os);
            write_line(checksum.value(), target->p, out);
            if (api.hashing())
            {
                success.fetch_add(1, std::memory_order_relaxed);
                if (verify)
                {
                    if (target->sum.empty())
                        _unsure.push_back(target.value());
                    else if (!same_digest(checksum.value(), target->sum))
                        _changed.push_back(target.value());
                }
            }
            else
            {
                unprocessed.fetch_add(1, std::memory_order_relaxed);
                if (verify)
                    _unsure.push_back(target.value());
            }
        }
    }
    if (verify)
    {
        std::lock_guard<std::mutex> lg(_mutex);
        changed.insert(changed.end(), _changed.begin(), _changed.end());
        unsure.insert(unsure.end(), _unsure.begin(), _unsure.end());
    }
}

void work_manager::printer()
{
    if (quiet)
        return;
    std::size_t width;
    std::size_t tick = 0;
    std::size_t last_size = 0;
    auto render = [&]() -> std::string
    {
        std::string line =
            "[" +
            std::to_string(success.load(std::memory_order_relaxed) +
                           failed.load(std::memory_order_relaxed) +
                           unprocessed.load(std::memory_order_relaxed)) +
            " files]";
        if (width > line.size() + 2)
        {
            const std::size_t inner = width - line.size() - 2;
            const std::size_t head = line.size() + 1;
            line.push_back('[');
            if (!interrupted.load(std::memory_order_relaxed))
            {
                line.append(inner, '.');
                line[head + tick % inner] = '#';
            }
            else
            {
                line.append(int_msg);
                line.resize(width - 1, '.');
            }
            line.push_back(']');
        }
        if (line.size() < last_size)
            line.append(last_size - line.size(), ' ');
        last_size = line.size();
        return line;
    };
    while (!stop_printer.load(std::memory_order_relaxed))
    {
        width = terminal_width(stderr) * 0.8;
        {
            std::osyncstream out(std::cerr);
            out << '\r' << render();
        }
        ++tick;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    {
        std::osyncstream out(std::cerr);
        out << '\r' << render() << '\n';
    }
}

void work_manager::wait()
{
    for (auto &it : workers)
        if (it.joinable())
            it.join();
    stop_printer.store(true, std::memory_order_relaxed);
    progress.join();
}

work_result work_manager::run(unsigned thread_count)
{
    if (thread_count == 0)
        throw std::invalid_argument(
            "tsubaki::work_manager::run: thread_count must be greater than 0");
    static_assert(std::atomic<bool>::is_always_lock_free);
    using sig_handler = void (*)(int);
    sig_handler previous = SIG_DFL;
    if (catch_sigint)
    {
        previous = std::signal(SIGINT, on_interrupt);
        g_interrupt = &interrupted;
    }
    workers.reserve(thread_count);
    auto steady_start = std::chrono::steady_clock::now();
    os << "# tsubaki checksum result" << std::endl;
    for (unsigned i = 0; i < thread_count; ++i)
        workers.emplace_back(&work_manager::worker, this, verify);
    progress = std::thread(&work_manager::printer, this);
    wait();
    auto steady_finish = std::chrono::steady_clock::now();
    auto duration = steady_finish - steady_start;
    if (catch_sigint)
    {
        g_interrupt = nullptr;
        std::signal(SIGINT, previous);
    }
    return work_result(api.name(), verify, tasks.take_errors(), changed, unsure,
                       success.load(), failed.load(), unprocessed.load(),
                       std::chrono::duration<double>(duration).count(),
                       static_cast<bool>(int_count.load()));
}

} // namespace tsubaki
