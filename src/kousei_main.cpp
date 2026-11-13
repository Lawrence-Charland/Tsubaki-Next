#include "timeline_fs.hpp"
#include <iostream>
int main(int argc, char **argv)
{
    kousei::timeline_fs t;
    auto root = t.root();
    // auto it = t.make_dir(0, root, "/");
    auto it1 = t.make_dir(0, root, "a");
    auto it2 = t.make_dir(0, root, "a/b");
    auto it3 = t.make_dir(0, root, "a/c");
    auto it4 = t.make_file(0, root, "a/c");
    return 0;
}