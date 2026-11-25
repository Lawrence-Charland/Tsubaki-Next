// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "utilities.hpp"
#include "checksum.hpp"

#include <algorithm>
#include <fstream>
#include <ios>
#include <istream>
#include <iterator>
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
    // No width floor: a list line always starts with the checksum, so there is
    // nothing to tell it apart from a path.
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

namespace
{
// An escaped line starts with a backslash and escapes only "\\" and "\n" inside.
std::string unescape_name(const std::string &name)
{
    std::string out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] == '\\' && i + 1 < name.size() &&
            (name[i + 1] == '\\' || name[i + 1] == 'n'))
        {
            out.push_back(name[i + 1] == 'n' ? '\n' : '\\');
            ++i;
            continue;
        }
        out.push_back(name[i]);
    }
    return out;
}
} // namespace

std::optional<task_token> parse_line(const std::string &line)
{
    bool escaped = false;
    std::size_t i = 0;
    if (!line.empty() && line[0] == '\\')
    {
        escaped = true;
        i = 1;
    }

    const std::size_t digest_begin = i;
    while (i < line.size() && line[i] != ' ')
        ++i;
    if (i == digest_begin)
        return {};

    const std::string digest(line, digest_begin, i - digest_begin);
    if (digest != none_checksum && !is_checksum(digest))
        return {};

    if (i == line.size())
        return {};

    // One space + '*' is the binary marker ( dropped ); two spaces keep the '*'.
    const std::size_t sep_begin = i;
    while (i < line.size() && line[i] == ' ')
        ++i;
    if (i == line.size())
        return {};
    if (i - sep_begin == 1 && line[i] == '*')
    {
        ++i;
        if (i == line.size())
            return {};
    }

    std::string path = line.substr(i);
    if (escaped)
        path = unescape_name(path);

    if (digest == none_checksum)
        return task_token(path);
    return task_token(path, digest);
}

void write_line(const std::string &sum, const std::filesystem::path &p,
                std::ostream &os)
{
    const std::string name = p.string();
    if (name.find_first_of("\n\\") != std::string::npos)
    {
        // Escaped: a leading backslash marks the line.
        os << '\\' << sum << "  ";
        for (char c : name)
        {
            if (c == '\\')
                os << "\\\\";
            else if (c == '\n')
                os << "\\n";
            else
                os << c;
        }
    }
    else
        os << sum << "  " << name;
    os << '\n';
}

std::string shown_name(const std::string &name)
{
    // A newline would split the "#" comment and leave a bare path; keep it escaped.
    std::string shown;
    shown.reserve(name.size());
    for (char c : name)
    {
        if (c == '\n')
            shown += "\\n";
        else
            shown += c;
    }
    return shown;
}

std::optional<std::string> get_file_content(std::istream &is, bool text)
{
    std::string key;
    try
    {
        // Byte-for-byte by default: whatever is in the stream is the key.
        key.assign(std::istreambuf_iterator<char>(is),
                   std::istreambuf_iterator<char>());
    }
    catch (const std::ios_base::failure &)
    {
        return {};
    }
    if (is.bad() || (is.fail() && !is.eof()))
        return {};
    if (text)
    {
        // Text mode means the same thing as $(cat key): CRLF counts as a line
        // ending rather than as content, and so does whatever trails the last
        // line. A lone CR stays, because it is not a CRLF.
        std::string normalized;
        normalized.reserve(key.size());
        for (std::size_t i = 0; i < key.size(); ++i)
        {
            if (key[i] == '\r' && i + 1 < key.size() && key[i + 1] == '\n')
                continue;
            normalized.push_back(key[i]);
        }
        while (!normalized.empty() &&
               (normalized.back() == '\n' || normalized.back() == '\r'))
            normalized.pop_back();
        key = std::move(normalized);
    }
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
// The logic associated with GNU Coreutils line format was assisted by DeepSeek.
