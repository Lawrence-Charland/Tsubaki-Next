// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "core_functions.hpp"
#include "exit_codes.hpp"
#include "utilities.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#endif

#if defined(_WIN32)
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
#if defined(_WIN32)
    // The manifest is a byte contract shared with coreutils and the POSIX
    // builds, so CRLF translation must not touch the standard streams.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    try
    {
#if defined(_WIN32)
        std::vector<std::string> args;
        args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i)
            args.push_back(from_fs_path(std::filesystem::path(argv[i])));
#else
        std::vector<std::string> args(argv, argv + argc);
#endif
        return tsubaki::tsubaki_entrance(args);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return TSUBAKI_UNEXPECTED;
    }
}
