// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "core_functions.hpp"
#include "exit_codes.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    try
    {
        std::vector<std::string> args(argv, argv + argc);
        return tsubaki::tsubaki_entrance(args);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return TSUBAKI_UNEXPECTED;
    }
}
