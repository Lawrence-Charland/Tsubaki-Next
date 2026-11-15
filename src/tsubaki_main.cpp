// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "sum_core.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        return tsubaki::sum_main(argc, argv);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return 1;
    }
}
