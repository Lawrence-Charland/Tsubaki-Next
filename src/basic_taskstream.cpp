// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "basic_taskstream.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <system_error>

namespace tsubaki
{
std::optional<std::filesystem::path> basic_taskstream::pop()
{
    for (;;)
    {
        std::optional<std::filesystem::path> candidate;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            candidate = pop_raw();
        }
        if (!candidate)
            return std::nullopt;

        std::error_code ec;
        const bool regular =
            std::filesystem::is_regular_file(candidate.value(), ec);
        if (ec)
        {
            // report_error locks the mutex ( safe to call here )
            report_error(error_entry(
                ec, "while fetching file type of " + candidate->string()));
            continue;
        }
        if (regular)
            return candidate;
        report_error(error_entry(
            std::make_error_code(std::errc::operation_not_permitted),
            "while trying to calculate " + candidate.value().string() +
                " which is not a regular file"));
    }
}

void basic_taskstream::report_error(const error_entry &error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    errors.push_back(error);
}

std::vector<error_entry> basic_taskstream::take_errors()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<error_entry> snapshot;
    snapshot.swap(errors);
    return snapshot;
}
} // namespace tsubaki
