// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_LISTSCANNER_HPP
#define LCPROJECTS_TSUBAKI_LISTSCANNER_HPP

#include "basic_taskstream.hpp"

#include <filesystem>
#include <istream>
#include <optional>
#include <ostream>

namespace tsubaki
{
class listscanner : public basic_taskstream
{
  private:
    std::istream &is;
    std::ostream &os;
    bool rescan;
    bool ended = false;   // 流已经读到终点（正常或出错），只报告一次

  public:
    listscanner(std::istream &_is, std::ostream &_os, bool _rescan = false)
        : is(_is), os(_os), rescan(_rescan)
    {
    }
    using basic_taskstream::pop;

  protected:
    std::optional<std::filesystem::path> pop_raw() override;
};
} // namespace tsubaki

#endif