// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "cached_taskstream.hpp"
#include <filesystem>

namespace tsubaki
{

void cached_taskstream::push(const std::filesystem::path &new_file)
{
    cache.push_back(new_file);
}

std::optional<std::filesystem::path> cached_taskstream::pop_raw()
{
    if (cache.empty())
        return {};
    std::filesystem::path result = cache.back();
    cache.pop_back();
    return result;
}
} // namespace tsubaki