// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "dirscanner.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace tsubaki
{
using namespace std::filesystem;

dirscanner::dirscanner(const std::filesystem::path &dir,
                       const std::vector<path> &_excludes, bool follow_symlink)
    : it(dir, directory_options::skip_permission_denied |
                  (follow_symlink ? directory_options::follow_directory_symlink
                                  : directory_options::none))
{
    if (!exists(dir))
        throw std::invalid_argument(
            "Error occurred while checking the existence of " + dir.string());
    if (!is_directory(dir))
        throw std::invalid_argument(dir.string() + " is not a directory");
    std::error_code ec;
    for (auto &it : _excludes)
    {
        if (!exists(it, ec))
        {
            errors.emplace_back(
                std::make_error_code(std::errc::no_such_file_or_directory),
                "while checking the existence of " + it.string());
            continue;
        }
        if (ec)
        {
            errors.emplace_back(ec, "while checking the existence of " +
                                        it.string());
            continue;
        }
        excludes.push_back(it);
    }
}

bool dirscanner::find_excludes()
{
    if (finished())
        return false;
    const auto &current = it->path();
    for (const auto &ex : excludes)
    {
        std::error_code ec;
        if (equivalent(ex, current, ec))
            return true;
        if (ec)
            errors.emplace_back(ec, "while checking if exclude " +
                                        current.string() +
                                        " by excluded item " + std::string(ex));
    }
    return false;
}

void dirscanner::skip_if_possible()
{
    while (find_excludes())
    {
        std::error_code ec;
        if ((*it).is_directory(ec))
            it.disable_recursion_pending();
        if (ec)
            errors.emplace_back(ec, "while checking if " + it->path().string() +
                                        " is a directory.");
        it++;
    }
}

bool dirscanner::finished() const
{
    return it == recursive_directory_iterator();
}

std::optional<path> dirscanner::pop_raw()
{
    skip_if_possible();
    if (finished())
        return {};
    path current = it->path();
    it++;
    return current;
}

std::optional<std::filesystem::path> dirscanner::pop()
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
        if (std::filesystem::is_regular_file(candidate.value(), ec))
            return candidate;
        if (ec)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            errors.emplace_back(ec, "while fetching file type of " +
                                        candidate->string());
        }
    }
}
} // namespace tsubaki