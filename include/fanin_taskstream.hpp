// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_FANIN_TASKSTREAM_HPP
#define LCPROJECTS_TSUBAKI_FANIN_TASKSTREAM_HPP

#include "basic_taskstream.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace tsubaki
{
class fanin_taskstream : public basic_taskstream
{
  private:
    std::vector<std::unique_ptr<basic_taskstream>> streams;
    std::atomic<std::size_t> cursor{0};

  public:
    void add_task(std::unique_ptr<basic_taskstream> task);
    std::optional<std::filesystem::path> pop() override;

  protected:
    std::optional<std::filesystem::path> pop_raw() override;
};
} // namespace tsubaki

#endif