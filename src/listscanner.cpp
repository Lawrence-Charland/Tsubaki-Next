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
    std::string line, result;
    std::pair<std::string, std::string> parsed_line;
    while (std::getline(is, line))
    {
        trim_str(line);
        if (line.empty() || line.at(0) == '#')
            continue;
        parsed_line = tsubaki::get_equation(line, ' ');
        if (!is_checksum(parsed_line.first))
            return std::filesystem::path(
                parsed_line.first == none_checksum ? parsed_line.second : line);
        else if (rescan)
            return std::filesystem::path(parsed_line.second);
        std::osyncstream(os) << "# Kept: " << parsed_line.second << std::endl
                             << line << std::endl;
    }
    return {};
}
} // namespace tsubaki