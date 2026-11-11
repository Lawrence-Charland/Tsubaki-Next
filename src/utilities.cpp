// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "utilities.hpp"

#include <algorithm>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

namespace tsubaki
{
std::pair<std::string, std::string> get_equation(const std::string &target,
                                                 char separator)
{
    size_t pos = target.find(separator);
    if (pos == target.npos)
        return {target, ""};
    return {target.substr(0, pos), target.substr(pos + 1)};
}

void trim_str(std::string &target)
{
    auto is_not_space = [](unsigned char c) { return !std::isspace(c); };
    target.erase(target.begin(),
                 std::find_if(target.begin(), target.end(), is_not_space));
    target.erase(
        std::find_if(target.rbegin(), target.rend(), is_not_space).base(),
        target.end());
}

bool is_checksum(const std::string &target)
{
    if (target.empty())
        return false;

    for (char c : target)
    {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                        (c >= 'A' && c <= 'F');
        if (!ok)
            return false;
    }
    return true;
}

std::string to_hex(const unsigned char *data, std::size_t len)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i)
    {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

std::size_t terminal_width(std::FILE *stream)
{
    winsize ws{};
    if (::ioctl(::fileno(stream), TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0)
        return 0;
    return static_cast<std::size_t>(ws.ws_col);
}

fd_guard::fd_guard(int fd) noexcept : fd_(fd)
{
}

fd_guard::~fd_guard()
{
    if (fd_ >= 0)
        ::close(fd_);
}

int fd_guard::get() const noexcept
{
    return fd_;
}
} // namespace tsubaki
