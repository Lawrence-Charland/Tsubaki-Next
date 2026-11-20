// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "utilities.hpp"
#include "checksum.hpp"

#include <algorithm>
#include <fstream>
#include <ios>
#include <istream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace tsubaki
{
std::pair<std::string, std::string> get_equation(const std::string &target,
                                                 char separator, bool trim)
{
    size_t pos = target.find(separator);
    if (pos == target.npos)
        return {target, ""};
    std::pair<std::string, std::string> result = {target.substr(0, pos),
                                                  target.substr(pos + 1)};
    if (trim)
        trim_str(result.first), trim_str(result.second);
    return result;
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

std::vector<std::string> get_env_arr(const std::string &env_name,
                                     char separator)
{
    std::vector<std::string> result;
    const char *raw = std::getenv(env_name.c_str());
    if (raw == nullptr)
        return result;
    const std::string value(raw);
    std::string::size_type start = 0;
    while (start <= value.size())
    {
        std::string::size_type pos = value.find(separator, start);
        if (pos == std::string::npos)
        {
            if (start < value.size())
                result.emplace_back(value, start);
            break;
        }
        if (pos > start)
            result.emplace_back(value, start, pos - start);
        start = pos + 1;
    }
    return result;
}

bool is_checksum(const std::string &target)
{
    if (target.length() < MIN_HASH_HEXLENGTH)
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

std::optional<std::string> get_file_content(std::istream &is, bool strip_crlf)
{
    std::string key;
    std::string line;
    bool first = true;
    try
    {
        while (std::getline(is, line))
        {
            if (strip_crlf && !line.empty() && line.back() == '\r')
                line.pop_back();
            if (!first)
                key.push_back('\n');
            key += line;
            first = false;
        }
    }
    catch (const std::ios_base::failure &)
    {
        return {};
    }
    if (is.bad() || (is.fail() && !is.eof()))
        return {};
    while (!key.empty() && (key.back() == '\n' || key.back() == '\r'))
        key.pop_back();
    if (key.empty())
        return {};
    return key;
}

std::optional<std::string>
get_comment_value(const std::filesystem::path &target, const std::string &key)
{
    std::string value, line;
    std::ifstream ifs(target);
    if (!ifs.good())
        return {};
    try
    {
        while (std::getline(ifs, line))
        {
            trim_str(line);
            if (line.empty() || line.at(0) != '#')
                continue;
            line = line.substr(1);
            auto eq = get_equation(line, ':');
            trim_str(eq.first);
            trim_str(eq.second);
            if (eq.first == key)
                return eq.second;
        }
    }
    catch (const std::ios_base::failure &)
    {
        return {};
    }
    return {};
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
