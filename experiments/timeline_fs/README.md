# timeline_fs (experimental)

Status: development paused on 2026-11-16. The module was moved out of the main
source tree and is kept as an experiment; nothing here is compiled, tested, or
referenced by the `tsubaki` target.

`timeline_fs` is a time-indexed in-memory filesystem intended for comparing
several sets of results. Every node carries one child list and one checksum map
per time id, so several snapshots can share a single tree instead of being
copied.

## Contents

| Path | Contents |
| --- | --- |
| `include/timeline_fs.hpp` | `node`, `timeline_fs` and the `tlfs_error` result type |
| `src/timeline_fs.cpp` | implementation of the tree operations |
| `src/tmptest.cpp` | scratch driver that fills a tree by hand; not a test |

## Bringing it back into the build

1. Move `include/timeline_fs.hpp` back to `include/` and `src/timeline_fs.cpp`
   (plus `src/tmptest.cpp` if still useful) back to `src/`.
2. Add `src/timeline_fs.cpp` to the `add_executable(tsubaki ...)` list in
   `CMakeLists.txt`.
3. Reconfigure and build; add a comparison subcommand and driver following the
   extension points in [ARCHITECTURE.md](../../ARCHITECTURE.md).
