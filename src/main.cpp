// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "tsubaki_core.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv)
{
    try
    {
        if (argc == 0)
            throw std::invalid_argument("argc is 0.");
        std::string program_name =
            std::filesystem::path(*argv).filename().string();
        if (program_name == "tsubaki")
            return tsubaki::tsubaki_main(argc, argv);
        else
        {
            std::cerr << "Error: unknown program name. " << std::endl;
            std::cerr << "あたしはいっぱい苦しんだんだ、あんたも苦しめ。苦しん"
                         "で苦しんで、私の事いっぱい考えろ。"
                      << std::endl;
            std::cerr
                << "I've been in so much pain. Now it's your turn to hurt. "
                   "Hurt until your mind is filled with nothing but me."
                << std::endl;
            return 1;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected error, what():" << std::endl
                  << e.what() << std::endl;
        return 1;
    }
}
