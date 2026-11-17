// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "worker.hpp"
#include "checksum.hpp"
#include "exit_codes.hpp"
#include "utilities.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <syncstream>
#include <thread>
#include <unistd.h>

namespace tsubaki
{
void work_result::print(std::ostream &os, bool with_errors)
{
    os << "# ---------- Summary ----------\n";
    os << "# Algorithm:         " << alg_to_str(alg) << std::endl;
    os << "# Processed:         " << success + failed << std::endl;
    os << "# Hashed:            " << success << std::endl;
    os << "# Failed:            " << failed << std::endl;
    os << "# Not hashed:        " << unprocessed << std::endl;
    os << "# Duration:          "
       << std::chrono::duration<double>(duration).count() << " seconds"
       << std::endl;
    os << "# Errors:            " << errors.size() << std::endl;
    if (interrupted)
        os << "#\n# Warning: The process was interrupted halfway through." << std::endl;
    if (!with_errors || errors.empty())
        return;
    os << "#\n# Error messages:" << std::endl;
    for (auto &it : errors)
    {
        os << "# Value: " << it.ec.value() << ", Message: " << it.ec.message()
           << ", Operation: " << it.op << std::endl;
    }
}

namespace
{
std::atomic<bool> *volatile g_interrupt = nullptr;

std::atomic<unsigned> volatile int_count = 0;

const constexpr char *term_msg =
    "\nTerminated after Ctrl+C was pressed 5 times.\n";

void on_interrupt(int)
{
    int_count++;
    if (g_interrupt)
        g_interrupt->store(true, std::memory_order_relaxed);
    if (int_count == 5)
    {
        write(2, term_msg, strlen(term_msg));
        _exit(TSUBAKI_TERMINATED);
    }
}
} // namespace

void work_manager::worker()
{
    std::optional<std::filesystem::path> target;
    while ((target = tasks.pop(), target))
    {
        if (interrupted.load(std::memory_order_relaxed))
        {
            std::osyncstream(os)
                << none_checksum << ' ' << target->string() << '\n';
            unprocessed.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        auto checksum = api.calc(target.value());
        if (!checksum)
        {
            tasks.report_error(
                error_entry(checksum.error(), "calculating the checksum of " +
                                                  target.value().string()));
            std::osyncstream(os)
                << none_checksum << ' ' << target->string() << '\n';
            failed.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            std::osyncstream(os)
                << checksum.value() << ' ' << target->string() << '\n';
            if (api.alg != algorithm::none)
                success.fetch_add(1, std::memory_order_relaxed);
            else
                unprocessed.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void work_manager::printer()
{
    if (quiet || api.alg == algorithm::none)
        return;
    std::size_t width;
    std::size_t tick = 0;
    std::size_t last_size = 0;
    auto render = [&]() -> std::string
    {
        std::string line =
            "[" + std::to_string(success.load(std::memory_order_relaxed)) +
            " files]";
        if (width > line.size() + 2)
        {
            const std::size_t inner = width - line.size() - 2;
            const std::size_t head = line.size() + 1;
            line.push_back('[');
            line.append(inner, '.');
            line[head + tick % inner] = '#';
            line.push_back(']');
        }
        if (line.size() < last_size)
            line.append(last_size - line.size(), ' ');
        last_size = line.size();
        return line;
    };
    while (!interrupted.load(std::memory_order_relaxed))
    {
        width = terminal_width(stderr);
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
    interrupted.store(true, std::memory_order_relaxed);
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
    for (unsigned i = 0; i < thread_count; ++i)
        workers.emplace_back(&work_manager::worker, this);
    progress = std::thread(&work_manager::printer, this);
    wait();
    auto steady_finish = std::chrono::steady_clock::now();
    auto duration = steady_finish - steady_start;
    if (catch_sigint)
    {
        g_interrupt = nullptr;
        std::signal(SIGINT, previous);
    }
    return work_result(api.alg, tasks.take_errors(), success.load(),
                       failed.load(), unprocessed.load(),
                       std::chrono::duration<double>(duration).count(),
                       static_cast<bool>(int_count.load()));
}

} // namespace tsubaki
