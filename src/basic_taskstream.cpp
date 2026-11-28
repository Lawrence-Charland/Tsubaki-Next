// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "basic_taskstream.hpp"
#include "utilities.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <system_error>

namespace tsubaki
{
std::optional<task_token> basic_taskstream::pop()
{
    for (;;)
    {
        std::optional<task_token> candidate;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            candidate = pop_raw();
        }
        if (!candidate)
            return std::nullopt;

        std::error_code ec;
        const std::filesystem::file_status status =
            std::filesystem::status(candidate->p, ec);
        if (ec)
        {
            // report_error locks the mutex ( safe to call here )
            report_error(error_entry(ec, "determining the file type of " +
                                             from_fs_path(candidate->p)));
            continue;
        }
        if (std::filesystem::is_regular_file(status))
            return candidate;
        if (std::filesystem::is_directory(status))
        {
            report_error(error_entry(
                std::make_error_code(std::errc::operation_not_permitted),
                "calculating the checksum of " + from_fs_path(candidate->p) +
                    ", which is a directory"));
            continue;
        }
        if (std::filesystem::is_socket(status))
        {
            report_error(error_entry(
                std::make_error_code(std::errc::operation_not_permitted),
                "calculating the checksum of " + from_fs_path(candidate->p) +
                    ", which is a socket"));
            continue;
        }
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
    std::vector<error_entry> snapshot;
    snapshot.swap(errors);
    return snapshot;
}
} // namespace tsubaki
