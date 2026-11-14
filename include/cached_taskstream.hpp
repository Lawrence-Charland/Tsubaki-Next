// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LC_PROJECTS_TSUBAKI_CACHED_TASKSTREAM_HPP
#define LC_PROJECTS_TSUBAKI_CACHED_TASKSTREAM_HPP

#include "basic_taskstream.hpp"

#include <filesystem>
#include <optional>
#include <queue>

namespace tsubaki
{
class cached_taskstream : public basic_taskstream
{
  private:
    std::queue<std::filesystem::path> cache;

  public:
    cached_taskstream(const std::queue<std::filesystem::path> &files)
        : cache(files)
    {
    }
    cached_taskstream() = default;
    void push(const std::filesystem::path &new_file);
    using basic_taskstream::pop;

  protected:
    std::optional<std::filesystem::path> pop_raw() override;
};
} // namespace tsubaki

#endif