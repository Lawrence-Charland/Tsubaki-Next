// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "fanin_taskstream.hpp"
#include "basic_taskstream.hpp"

#include <mutex>
#include <utility>

namespace tsubaki
{
void fanin_taskstream::add_task(std::unique_ptr<basic_taskstream> task)
{
    std::lock_guard<std::mutex> lock(mutex_);
    streams.push_back(std::move(task));
}

std::optional<std::filesystem::path> fanin_taskstream::pop()
{
    for (;;)
    {
        const std::size_t index = cursor.load(std::memory_order_acquire);
        if (index >= streams.size())
            return {};
        if (auto result = streams.at(index)->pop())
            return result;
        std::size_t expected = index;
        if (cursor.compare_exchange_strong(expected, index + 1,
                                           std::memory_order_acq_rel))
        {
            auto incoming = streams.at(index)->take_errors();
            std::lock_guard<std::mutex> lock(mutex_);
            errors.insert(errors.end(), incoming.begin(), incoming.end());
        }
    }
}

std::optional<std::filesystem::path> fanin_taskstream::pop_raw()
{
    return pop();
}
} // namespace tsubaki
