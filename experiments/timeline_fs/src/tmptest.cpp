// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

// Scratch driver for timeline_fs: exercises the tree by hand, asserts nothing.
// Not a test and not built; kept with the module as a sketch of how a
// comparison would fill it.

#include "timeline_fs.hpp"
#include <iostream>
int main(int argc, char **argv)
{
    tsubaki::timeline_fs t;
    auto root = t.root();
    // auto it = t.make_dir(0, root, "/");
    auto it1 = t.make_dir(0, root, "a");
    auto it2 = t.make_dir(0, root, "a/b");
    auto it3 = t.make_dir(0, root, "a/c");
    auto it4 = t.make_file(0, root, "a/c");
    return 0;
}