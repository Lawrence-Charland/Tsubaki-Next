#ifndef LCPROJECTS_TSUBAKI_KOUSEI_TIMELINE_FS_HPP
#define LCPROJECTS_TSUBAKI_KOUSEI_TIMELINE_FS_HPP

#include <expected>
#include <filesystem>
#include <list>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kousei
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

struct error_entry
{
    err_code ec;
    std::string ctx;

    error_entry(err_code _ec, const std::string &_ctx) : ec(_ec), ctx(_ctx)
    {
    }
};

struct node;

typedef std::list<node>::iterator node_ptr;
typedef std::list<node>::const_iterator const_node_ptr;

struct node
{
  public:
    path name;
    file_type type;
    std::list<node>::iterator parent_node;
    std::unordered_map<unsigned short, std::string> checksums;
    std::unordered_map<unsigned short, std::vector<node_ptr>> subnodes;

    node(const std::filesystem::path &_name, file_type _type);
    std::expected<node_ptr, error_entry> find_subnode(unsigned short time_id,
                                                      const path &node_name);
    std::expected<std::list<node>::const_iterator, error_entry>
    find_subnode(unsigned short time_id, const path &node_name) const;
    std::expected<void, error_entry> touch_subnode(unsigned short time_id,
                                                   const node_ptr &new_node);
    std::expected<std::string_view, error_entry>
    inquire_checksum(unsigned short time_id) const;
    void print_subnodes(unsigned short time_id, std::ostream &os) const;

    class iterator
    {
    };
};

class timeline_fs
{
  private:
    std::unordered_map<std::string, unsigned short> timemapper;
    std::list<node> node_list;
    void remove_node_internal(unsigned short time_id, const node_ptr &node_);
    std::expected<node_ptr, error_entry>
    inquire_node_mutable(unsigned short time_id, const node_ptr &from,
                         const path &relative_path);

  public:
    timeline_fs();
    node_ptr root();
    const_node_ptr root() const;
    std::expected<unsigned short, error_entry> add_time(const std::string &exp);
    std::expected<const_node_ptr, error_entry>
    inquire_node(unsigned short time_id, const_node_ptr from,
                 const path &relative_path) const;
    std::expected<node_ptr, error_entry> touch_node(unsigned short time_id,
                                                    const node_ptr &ptr,
                                                    const path &node_name,
                                                    file_type type);
    std::expected<node_ptr, error_entry> make_dir(unsigned short time_id,
                                                  const node_ptr &from,
                                                  const path &relative_path);
    std::expected<node_ptr, error_entry> make_file(unsigned short time_id,
                                                   const node_ptr &from,
                                                   const path &relative_path);
    std::expected<void, error_entry> remove_node(unsigned short time_id,
                                                 const node_ptr &from,
                                                 const path &relative_path);
    std::expected<void, error_entry>
    update_checksum(unsigned short time_id, const node_ptr node_,
                    const std::string &checksum);
    std::expected<std::string_view, error_entry>
    inquire_checksum(unsigned short time_id, const_node_ptr from,
                     const path &relative_path) const;
};
} // namespace kousei

#endif