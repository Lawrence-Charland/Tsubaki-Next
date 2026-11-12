// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_DIRSCANNER_HPP
#define LCPROJECTS_TSUBAKI_DIRSCANNER_HPP

#include "basic_taskstream.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace tsubaki
{
class dirscanner : public basic_taskstream
{
  private:
    std::filesystem::recursive_directory_iterator it;
    std::vector<std::filesystem::path> excludes;
    bool find_excludes();
    void skip_if_possible();
    bool finished() const;

  public:
    dirscanner(const std::filesystem::path &dir,
               const std::vector<std::filesystem::path> &_excludes,
               bool follow_symlink = false);
    std::optional<std::filesystem::path> pop() override;

  protected:
    std::optional<std::filesystem::path> pop_raw() override;
};
} // namespace tsubaki

#endif
