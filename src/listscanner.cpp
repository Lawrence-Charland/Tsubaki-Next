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
        trim_str(line);
        if (line.empty() || line.at(0) == '#')
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
        // A real digest is only returned when rescanning; a "<NONE>" line names
        // a file to hash either way.
        if (rescan || parsed->sum.empty())
            return parsed.value();
        std::osyncstream(os)
            << "# Kept: " << shown_name(parsed->p.string()) << std::endl
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
                                             candidate->p.string()));
            continue;
        }
        if (std::filesystem::is_regular_file(status))
            return candidate;
        if (std::filesystem::is_directory(status))
        {
            report_error(error_entry(
                std::make_error_code(std::errc::operation_not_permitted),
                "calculating the checksum of " + candidate->p.string() +
                    ", which is a directory"));
            continue;
        }
        if (std::filesystem::is_socket(status))
        {
            report_error(error_entry(
                std::make_error_code(std::errc::operation_not_permitted),
                "calculating the checksum of " + candidate->p.string() +
                    ", which is a socket"));
            continue;
        }
        return candidate;
    }
}
} // namespace tsubaki