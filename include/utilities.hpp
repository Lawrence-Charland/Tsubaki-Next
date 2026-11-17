// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_UTILITIES
#define LCPROJECTS_TSUBAKI_UTILITIES

#include <cstddef>
#include <cstdio>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>

namespace tsubaki
{
std::pair<std::string, std::string>
get_equation(const std::string &target, char separator, bool trim = true);

void trim_str(std::string &target);

bool is_checksum(const std::string &target);

// Reads a key from `is`, keeping interior bytes as they are and dropping any
// run of trailing CR/LF. An unreadable stream or a key that ends up empty is
// reported as std::nullopt.
std::optional<std::string> get_file_content(std::istream &is);

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
