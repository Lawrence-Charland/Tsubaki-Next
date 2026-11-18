// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_LISTSCANNER_HPP
#define LCPROJECTS_TSUBAKI_LISTSCANNER_HPP

#include "basic_taskstream.hpp"

#include <istream>
#include <optional>
#include <ostream>
#include <string>

namespace tsubaki
{
class listscanner : public basic_taskstream
{
  private:
    std::istream &is;
    std::ostream &os;
    std::string pretty_name;
    bool rescan;
    bool trust;
    bool ended = false;

  public:
    listscanner(const std::string &_name, std::istream &_is, std::ostream &_os,
                bool _rescan, bool _trust)
        : is(_is), os(_os), pretty_name(_name), rescan(_rescan), trust(_trust)
    {
    }

    std::optional<task_token> pop() override;

  protected:
    std::optional<task_token> pop_raw() override;
};
} // namespace tsubaki

#endif