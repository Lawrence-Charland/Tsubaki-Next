// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "basic_taskstream.hpp"
#include "cmdline.hpp"
#include "core_functions.hpp"
#include "exit_codes.hpp"
#include "listscanner.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace tsubaki
{
namespace
{
struct cmp_entry
{
    std::filesystem::path p;
    std::string sum;
    std::string shown;
    bool left = true;
    bool mark = false;
};
} // namespace

#define TSUBAKI_IMPORT_LIST(SRC, LEFT)                                         \
    {                                                                          \
        std::istream *input;                                                   \
        std::ifstream ifs_input;                                               \
        if ((SRC) == "-")                                                      \
            input = &std::cin;                                                 \
        else                                                                   \
        {                                                                      \
            ifs_input.open(path(SRC));                                         \
            if (!ifs_input.good())                                             \
            {                                                                  \
                std::cerr << "Error: cannot read file list \"" << (SRC)        \
                          << "\"." << std::endl;                               \
                return TSUBAKI_FILE_READ_FAILED;                               \
            }                                                                  \
            input = &ifs_input;                                                \
        }                                                                      \
        listscanner file_list((SRC) == "-" ? "stdin" : (SRC), *input,          \
                              std::cerr, true, true);                          \
        while (auto token = file_list.pop())                                   \
        {                                                                      \
            if (token->sum.empty())                                            \
            {                                                                  \
                errors.emplace_back(                                           \
                    std::make_error_code(std::errc::invalid_argument),         \
                    "empty checksum of \"" + token->p.string() + "\"");        \
                continue;                                                      \
            }                                                                  \
            cmp_entry entry;                                                   \
            entry.shown = token->p.string();                                   \
            entry.p = token->p;                                                \
            if (_canonical)                                                    \
            {                                                                  \
                std::error_code ec;                                            \
                entry.p = canonical(entry.p, ec);                              \
                if (ec)                                                        \
                {                                                              \
                    errors.emplace_back(ec, "canonicalizing \"" +              \
                                                entry.shown + "\"");           \
                    continue;                                                  \
                }                                                              \
            }                                                                  \
            entry.sum = token->sum;                                            \
            for (char &c : entry.sum)                                          \
                c = static_cast<char>(                                         \
                    std::tolower(static_cast<unsigned char>(c)));              \
            entry.left = LEFT;                                                 \
            entries.push_back(std::move(entry));                               \
        }                                                                      \
        auto get_errors = file_list.take_errors();                             \
        errors.insert(errors.end(), get_errors.begin(), get_errors.end());     \
    }

int cmp_main(argparser::ap_instance &cmdline)
{
    using namespace std::filesystem;

    cmdline.evaluate(cmdline_group::cmp);
    if (!cmdline.fine(false))
    {
        error_prompt(std::cerr, cmdline);
        return TSUBAKI_INVALID_OPERATION;
    }

    std::vector<cmp_entry> entries;
    std::vector<error_entry> errors;
    std::ostream *output;
    std::ofstream ofs_output;
    bool _canonical =
        static_cast<bool>(cmdline.entry(cmdline_entry::canonical).repetition);

    std::string L_src = cmdline.entry(cmdline_entry::to_compare).content.at(0),
                R_src = cmdline.entry(cmdline_entry::to_compare).content.at(1);
    {
        // Decide where to output
        {
            auto &content = cmdline.entry(cmdline_entry::output).content;
            if (!content.empty() && content.at(0) == "-")
                output = &std::cout;
            else
            {
                path output_filepath;
                if (content.empty()) // L_to_R.cmp
                    output_filepath =
                        path(".") /
                        path(path(L_src).filename().stem().string() + "_to_" +
                             path(R_src).filename().stem().string() + ".cmp");
                else
                    output_filepath = path(content.at(0));
                // Both lists are read after the sink is opened, so an output
                // naming either of them would truncate it first.
                for (const std::string *src : {&L_src, &R_src})
                {
                    if (*src == "-")
                        continue;
                    std::error_code ec;
                    if (equivalent(path(*src), output_filepath, ec))
                    {
                        std::cerr << "Error: the output file must not be one "
                                     "of the input files."
                                  << std::endl;
                        return TSUBAKI_INVALID_OPERATION;
                    }
                }
                ofs_output.open(output_filepath);
                if (!ofs_output.good())
                {
                    std::cerr << "Error: cannot write to \""
                              << output_filepath.string() << "\"." << std::endl;
                    return TSUBAKI_FILE_WRITE_FAILED;
                }
                output = &ofs_output;
                std::cerr << "The output file will be \""
                          << output_filepath.string() << "\"." << std::endl;
            }
        }
        // Import the file lists
        {
            if (L_src == "-" && R_src == "-")
            {
                std::cerr
                    << "Error: the left and right list cannot be stdin at "
                       "the same time."
                    << std::endl;
                return TSUBAKI_INVALID_OPERATION;
            }
            TSUBAKI_IMPORT_LIST(L_src, true);
            TSUBAKI_IMPORT_LIST(R_src, false);
        }
        // Duplication
        {
            std::sort(entries.begin(), entries.end(),
                      [](const cmp_entry &lhs, const cmp_entry &rhs) -> bool
                      {
                          if (lhs.p != rhs.p)
                              return lhs.p < rhs.p;
                          return lhs.left > rhs.left;
                      });
            for (size_t i = 1; i < entries.size(); ++i)
            {
                if (entries.at(i - 1).p != entries.at(i).p ||
                    entries.at(i - 1).left != entries.at(i).left)
                    entries.at(i).mark = 0;
                else if (entries.at(i - 1).sum != entries.at(i).sum)
                {
                    std::cerr << "Error: \"" << entries.at(i).p.string()
                              << "\" appears in one side more than once with "
                                 "multiple checksums."
                              << std::endl;
                    return TSUBAKI_INVALID_OPERATION;
                }
                else
                    entries.at(i).mark = entries.at(i - 1).mark + 1;
            }
        }
        // This line should be out of any scope.
        // But clang-format has a bug -_-
        {
            *output << "# tsubaki comparison result" << std::endl;
            std::cerr << "# L = " << (L_src == "-" ? "stdin" : L_src)
                      << ", R = " << (R_src == "-" ? "stdin" : R_src)
                      << std::endl;
        }
    }
    // Compare the imported lists
    {
        // Entries the dedup pass flagged with 0 are not compared at all.
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [](const cmp_entry &entry)
                                     { return entry.mark; }),
                      entries.end());
        // A path is kept at most once per side, so sorting by path and side is
        // enough: two paths in a row name the same file on both sides.
        std::sort(entries.begin(), entries.end(),
                  [](const cmp_entry &lhs, const cmp_entry &rhs) -> bool
                  {
                      if (lhs.p != rhs.p)
                          return lhs.p < rhs.p;
                      // The left spelling is reported when both lists name the
                      // same file differently.
                      return lhs.left > rhs.left;
                  });

        // Phase 1: a path present in both lists is either matched or modified;
        // a path with only one side is left for phase 2.
        std::vector<std::string> matched, modified;
        std::vector<cmp_entry> rest;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (i + 1 < entries.size() && entries[i].p == entries[i + 1].p)
            {
                if (entries[i].sum == entries[i + 1].sum)
                    matched.push_back(entries[i].shown);
                else
                    modified.push_back(entries[i].shown);
                ++i;
            }
            else
                rest.push_back(entries[i]);
        }

        *output << "# [!] Modified:" << std::endl;
        for (const auto &it : modified)
            *output << "[!] " << it << std::endl;
        *output << std::endl
                << "# L = " << (L_src == "-" ? "stdin" : L_src)
                << ", R = " << (R_src == "-" ? "stdin" : R_src) << std::endl;
        *output << "# [D] Moved,copied,merged or renamed:" << std::endl;

        // Phase 2: whatever is left is grouped by checksum. A checksum held by
        // only one side was deleted or added; one held by both sides names the
        // same content at different paths.
        std::sort(rest.begin(), rest.end(),
                  [](const cmp_entry &lhs, const cmp_entry &rhs) -> bool
                  {
                      if (lhs.sum != rhs.sum)
                          return lhs.sum < rhs.sum;
                      if (lhs.p != rhs.p)
                          return lhs.p < rhs.p;
                      return lhs.shown < rhs.shown;
                  });

        std::vector<std::string> left_only, right_only;
        unsigned groups = 0;
        for (std::size_t first = 0; first < rest.size();)
        {
            std::size_t last = first;
            while (last < rest.size() && rest[last].sum == rest[first].sum)
                ++last;

            std::vector<std::string> left_paths, right_paths;
            for (std::size_t i = first; i < last; ++i)
            {
                if (rest[i].left)
                    left_paths.push_back(rest[i].shown);
                else
                    right_paths.push_back(rest[i].shown);
            }
            if (left_paths.empty())
                right_only.insert(right_only.end(), right_paths.begin(),
                                  right_paths.end());
            else if (right_paths.empty())
                left_only.insert(left_only.end(), left_paths.begin(),
                                 left_paths.end());
            else
            {
                const unsigned id = ++groups;
                *output << "# [D][" << id << ']' << std::endl
                        << "# [D][" << id << "][L]" << std::endl;
                for (const auto &it : left_paths)
                    *output << "[D][" << id << "][L]" << it << std::endl;
                *output << "# [D][" << id << "][R]" << std::endl;
                for (const auto &it : right_paths)
                    *output << "[D][" << id << "][R]" << it << std::endl;
                *output << std::endl;
            }
            first = last;
        }

        *output << std::endl
                << "# L = " << (L_src == "-" ? "stdin" : L_src)
                << ", R = " << (R_src == "-" ? "stdin" : R_src) << std::endl;
        *output << "# [U] Deleted or added:" << std::endl
                << "# [U][L]" << std::endl;
        for (const auto &it : left_only)
            *output << "[U][L]" << it << std::endl;
        *output << "# [U][R]" << std::endl;
        for (const auto &it : right_only)
            *output << "[U][R]" << it << std::endl;

        *output << std::endl << "# [=] Matched:" << std::endl;
        for (const auto &it : matched)
            *output << "[=] " << it << std::endl;

        if (!errors.empty())
        {
            *output << std::endl << "# [E] Error messages:" << std::endl;
            for (const auto &error : errors)
                *output << "[E] Value: " << error.ec.value()
                        << ", Message: " << error.ec.message()
                        << ", Operation: " << error.op << std::endl;
        }
    }

    output->flush();
    if (output->fail())
    {
        std::cerr << "Error: failed to write output; it may be incomplete."
                  << std::endl;
        return TSUBAKI_FILE_WRITE_FAILED;
    }
    if (errors.size() > 0)
    {
        std::cerr << "Error: several problems were reported; the collected "
                     "messages are at the end of the output."
                  << std::endl;
        return TSUBAKI_HASH_ERROR;
    }
    return TSUBAKI_SUCCESS;
}
} // namespace tsubaki
