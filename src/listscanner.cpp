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
std::optional<std::filesystem::path> listscanner::pop_raw()
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
        parsed_line = get_equation(line, ' ');
        if (parsed_line.second.empty())
            return std::filesystem::path(line);
        if (!is_checksum(parsed_line.first))
            return std::filesystem::path(
                parsed_line.first == none_checksum ? parsed_line.second : line);
        else if (rescan)
            return std::filesystem::path(parsed_line.second);
        std::osyncstream(os) << "# Kept: " << parsed_line.second << std::endl
                             << line << std::endl;
    }
    if (is.fail() && !is.eof())
        errors.emplace_back(std::make_error_code(std::errc::io_error),
                            "while reading file list");
    ended = true;
    return {};
}
} // namespace tsubaki