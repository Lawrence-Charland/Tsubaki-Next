// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_UTILITIES_HPP
#define LCPROJECTS_TSUBAKI_UTILITIES_HPP

#include "basic_taskstream.hpp"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tsubaki
{
std::pair<std::string, std::string>
get_equation(const std::string &target, char separator, bool trim = true);

void trim_str(std::string &target);

std::vector<std::string> get_env_arr(const std::string &env_name,
                                     char separator);

// The only two places UTF-8 text and filesystem paths are exchanged.
std::filesystem::path to_fs_path(const std::string &text);

std::string from_fs_path(const std::filesystem::path &p);

bool is_checksum(const std::string &target);

std::optional<task_token> parse_line(const std::string &line);

void write_line(const std::string &sum, const std::filesystem::path &p,
                std::ostream &os);

std::string shown_name(const std::string &name);

std::optional<std::string> get_file_content(std::istream &is, bool text);

std::optional<std::string>
get_comment_value(const std::filesystem::path &target, const std::string &key);

std::string to_hex(const unsigned char *data, std::size_t len);

std::size_t terminal_width(std::FILE *stream = stdout);

class fd_guard
{
  public:
    explicit fd_guard(int fd) noexcept;
    ~fd_guard();

    fd_guard(const fd_guard &) = delete;
    fd_guard &operator=(const fd_guard &) = delete;

    int get() const noexcept;

  private:
    int fd_;
};
} // namespace tsubaki

#endif
