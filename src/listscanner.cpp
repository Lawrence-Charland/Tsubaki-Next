// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "listscanner.hpp"
#include "checksum.hpp"
#include "utilities.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <syncstream>
#include <utility>

namespace tsubaki
{
std::string listscanner::format_error(const std::string &line)
{
    std::string message =
        "not a \"<checksum> <path>\" line in " + pretty_name + ": " + line;
    if (!explained_bare_path)
    {
        // One hint per file, attached to the first bad line: it is the shape
        // change users are most likely to run into.
        message +=
            "; a bare path list is no longer a listing: pass the paths on the "
            "command line, or prefix each line with \"<NONE> \" "
            "(sed 's/^/<NONE> /' list.txt | tsubaki sum <algorithm> -)";
        explained_bare_path = true;
    }
    return message;
}

std::optional<task_token> listscanner::pop_raw()
{
    if (ended)
        return {};
    std::string line;
    std::pair<std::string, std::string> parsed_line;
    while (std::getline(is, line))
    {
        trim_str(line);
        if (line.empty() || line.at(0) == '#')
            continue;
        parsed_line = get_equation(line, ' ', true);
        if (parsed_line.second.empty())
        {
            errors.emplace_back(
                std::make_error_code(std::errc::invalid_argument),
                format_error(line));
            continue;
        }
        if (parsed_line.first == none_checksum)
            return task_token(parsed_line.second);
        if (!is_checksum(parsed_line.first))
        {
            errors.emplace_back(
                std::make_error_code(std::errc::invalid_argument),
                format_error(line));
            continue;
        }
        const std::size_t sep = parsed_line.first.size();
        if (line.size() > sep + 1 && line.at(sep) == ' ' &&
            line.at(sep + 1) == '*')
            parsed_line.second.erase(0, 1);
        if (rescan)
            return task_token(parsed_line.second, parsed_line.first);
        std::osyncstream(os) << "# Kept: " << parsed_line.second << std::endl
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