// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_BASIC_TASKSTREAM_HPP
#define LCPROJECTS_TSUBAKI_BASIC_TASKSTREAM_HPP

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace tsubaki
{
struct task_token
{
    std::filesystem::path p;
    std::string sum;

    task_token() = default;
    task_token(const std::filesystem::path &_p) : p(_p)
    {
    }
    task_token(const std::filesystem::path &_p, const std::string &_sum)
        : p(_p), sum(_sum)
    {
    }
};

struct error_entry
{
    std::error_code ec;
    std::string op;
    error_entry(const std::error_code &_ec, std::string _op)
        : ec(_ec), op(_op) {};
};

class basic_taskstream
{
  public:
    void report_error(const error_entry &error);
    virtual std::optional<task_token> pop();
    virtual std::vector<error_entry> take_errors();
    virtual ~basic_taskstream() = default;

  protected:
    virtual std::optional<task_token> pop_raw() = 0;
    mutable std::mutex mutex_;
    std::vector<error_entry> errors;
};
} // namespace tsubaki

#endif
