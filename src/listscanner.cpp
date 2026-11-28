// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "listscanner.hpp"
#include "utilities.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <syncstream>

namespace tsubaki
{
std::optional<task_token> listscanner::pop_raw()
{
    if (ended)
        return {};
    std::string line;
    while (std::getline(is, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        auto parsed = parse_line(line);
        if (!parsed)
        {
            errors.emplace_back(
                std::make_error_code(std::errc::invalid_argument),
                "not a \"<checksum> <path>\" line in " + pretty_name + ": " +
                    line);
            continue;
        }
        // "<NONE>" names a file to hash either way; a digest only matters on rescan.
        if (rescan || parsed->sum.empty())
            return parsed.value();
        std::osyncstream(os)
            << "# Kept: " << shown_name(from_fs_path(parsed->p)) << std::endl
            << line << std::endl;
    }
    if (is.fail() && !is.eof())
        errors.emplace_back(std::make_error_code(std::errc::io_error),
                            "reading file list " + pretty_name);
    ended = true;
    return {};
}

std::optional<task_token> listscanner::pop()
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

        if (trust)
            return candidate;
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
} // namespace tsubaki