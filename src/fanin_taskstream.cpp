// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "fanin_taskstream.hpp"
#include "basic_taskstream.hpp"

#include <filesystem>
#include <mutex>
#include <pthread.h>
#include <utility>
#include <vector>

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
        cursor.compare_exchange_strong(expected, index + 1,
                                       std::memory_order_acq_rel);
    }
}

std::vector<error_entry> fanin_taskstream::take_errors()
{
    std::vector<error_entry> tmp, result;
    result.swap(errors);
    for (auto &it : streams)
    {
        tmp = it->take_errors();
        result.insert(result.end(), tmp.begin(), tmp.end());
    }
    return result;
}

std::optional<std::filesystem::path> fanin_taskstream::pop_raw()
{
    return pop();
}
} // namespace tsubaki
