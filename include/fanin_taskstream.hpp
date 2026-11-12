// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_FANIN_TASKSTREAM_HPP
#define LCPROJECTS_TSUBAKI_FANIN_TASKSTREAM_HPP

#include "basic_taskstream.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <queue>

namespace tsubaki
{
class fanin_taskstream : public basic_taskstream
{
  private:
    std::queue<std::unique_ptr<basic_taskstream>> task_queue;

  public:
    void add_task(std::unique_ptr<basic_taskstream> task);
    using basic_taskstream::pop;

  protected:
    std::optional<std::filesystem::path> pop_raw() override;
};
} // namespace tsubaki

#endif