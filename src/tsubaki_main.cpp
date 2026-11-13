// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "tsubaki_core.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        return tsubaki::tsubaki_main(argc, argv);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected error, what():" << std::endl
                  << e.what() << std::endl;
        return 1;
    }
}
