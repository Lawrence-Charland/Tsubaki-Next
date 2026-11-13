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
        const std::filesystem::file_status status =
            std::filesystem::status(candidate.value(), ec);
        if (ec)
        {
            // report_error locks the mutex ( safe to call here )
            report_error(error_entry(
                ec, "while fetching file type of " + candidate->string()));
            continue;
        }
        if (std::filesystem::is_regular_file(status))
            return candidate;
        if (std::filesystem::is_directory(status))
        {
            report_error(error_entry(
                std::make_error_code(std::errc::operation_not_permitted),
                "while trying to calculate " + candidate.value().string() +
                    " which is a directory"));
            continue;
        }
        // Devices, FIFOs and sockets are handed out as they are: reading one is
        // the caller's explicit decision, and only the caller knows whether it
        // reaches EOF. Directory walks still filter them in dirscanner::pop().
        return candidate;
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
