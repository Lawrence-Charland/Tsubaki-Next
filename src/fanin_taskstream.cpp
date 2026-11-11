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
    task_queue.push(std::move(task));
}

std::optional<std::filesystem::path> fanin_taskstream::pop_raw()
{
    while (!task_queue.empty())
    {
        basic_taskstream &current = *task_queue.front();
        if (auto result = current.pop())
            return result;

        auto incoming = current.take_errors();
        errors.insert(errors.end(), incoming.begin(), incoming.end());
        task_queue.pop();
    }
    return {};
}
} // namespace tsubaki
