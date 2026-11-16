// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

// Time-indexed in-memory filesystem: groundwork for comparing several sets of
// results. Experimental: development is paused and the module is not part of the
// build. Each node keeps one child list and one checksum per time id, so several
// snapshots share a single tree instead of being copied. See the README in this
// directory for how to bring it back into the build.

#ifndef LCPROJECTS_TSUBAKI_TIMELINE_FS_HPP
#define LCPROJECTS_TSUBAKI_TIMELINE_FS_HPP

#include <expected>
#include <filesystem>
#include <list>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tsubaki
{
using namespace std::filesystem;

enum class err_code
{
    no_error,
    unknown_time_id,
    file_or_dir_unfound,
    file_or_dir_exists,
    not_a_directory,
    not_a_regular,
    not_a_file_name,
    invalid_operation,
    unknown_error,
};

struct tlfs_error
{
    err_code ec;
    std::string ctx;

    tlfs_error(err_code _ec, const std::string &_ctx) : ec(_ec), ctx(_ctx)
    {
    }
};

struct node;

typedef std::list<node>::iterator node_ptr;
typedef std::list<node>::const_iterator const_node_ptr;
typedef std::vector<node_ptr>::iterator node_it;
typedef unsigned short time_id_t;

struct node
{
  public:
    path name;
    file_type type;
    node_ptr parent_node;
    std::unordered_map<time_id_t, std::string> checksums;
    std::unordered_map<time_id_t, std::vector<node_ptr>> subnodes;

    node(const std::filesystem::path &_name, file_type _type);
    std::expected<node_ptr, tlfs_error> find_subnode(time_id_t time_id,
                                                     const path &node_name);
    std::expected<const_node_ptr, tlfs_error>
    find_subnode(time_id_t time_id, const path &node_name) const;
    std::expected<void, tlfs_error> touch_subnode(time_id_t time_id,
                                                  const node_ptr &new_node);
    std::expected<std::string_view, tlfs_error>
    inquire_checksum(time_id_t time_id) const;
    void print_subnodes(time_id_t time_id, std::ostream &os) const;
};

class timeline_fs
{
  private:
    std::unordered_map<std::string, time_id_t> timemapper;
    std::list<node> node_list;
    void remove_node_internal(time_id_t time_id, const node_ptr &node_);
    std::expected<node_ptr, tlfs_error>
    inquire_node_mutable(time_id_t time_id, const node_ptr &from,
                         const path &relative_path);

  public:
    timeline_fs();
    node_ptr root();
    const_node_ptr root() const;
    std::expected<time_id_t, tlfs_error> add_time(const std::string &exp);
    std::expected<const_node_ptr, tlfs_error>
    inquire_node(time_id_t time_id, const_node_ptr from,
                 const path &relative_path) const;
    std::expected<node_ptr, tlfs_error> touch_node(time_id_t time_id,
                                                   const node_ptr &ptr,
                                                   const path &node_name,
                                                   file_type type);
    std::expected<node_ptr, tlfs_error> make_dir(time_id_t time_id,
                                                 const node_ptr &from,
                                                 const path &relative_path);
    std::expected<node_ptr, tlfs_error> make_file(time_id_t time_id,
                                                  const node_ptr &from,
                                                  const path &relative_path);
    std::expected<void, tlfs_error> remove_node(time_id_t time_id,
                                                const node_ptr &from,
                                                const path &relative_path);
    std::expected<void, tlfs_error>
    update_checksum(time_id_t time_id, const node_ptr node_,
                    const std::string &checksum);
    std::expected<std::string_view, tlfs_error>
    inquire_checksum(time_id_t time_id, const_node_ptr from,
                     const path &relative_path) const;
};
} // namespace tsubaki

#endif
