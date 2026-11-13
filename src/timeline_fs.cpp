#include "timeline_fs.hpp"
#include <climits>
#include <exception>
#include <expected>
#include <filesystem>
#include <iterator>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string_view>

namespace kousei
{
using namespace std::filesystem;

namespace
{
bool is_single_name(const path &p)
{
    return !p.empty() && std::distance(p.begin(), p.end()) == 1;
}

template <typename NODE_PTR>
std::expected<NODE_PTR, error_entry>
walk_path(unsigned short time_id, NODE_PTR cursor, const path &relative_path)
{
    for (auto it = relative_path.begin(); it != relative_path.end(); it++)
    {
        if (it->empty())
            break;
        if (*it == ".")
            continue;
        if (*it == "..")
        {
            cursor = cursor->parent_node;
            continue;
        }
        auto find = cursor->find_subnode(time_id, *it);
        if (!find)
            return std::unexpected(find.error());
        cursor = find.value();
    }
    return cursor;
}

unsigned short randum_time_id()
{
    thread_local std::mt19937_64 gen{std::random_device{}()};
    thread_local std::uniform_int_distribution<unsigned short> dist;
    return dist(gen);
}
} // namespace

// ---------- Filesystem node ----------

node::node(const path &_name, file_type _type) : name(_name), type(_type)
{
    if (!is_single_name(_name))
        throw std::invalid_argument("\"" + _name.string() +
                                    "\" is not a file name");
    if (_type != file_type::regular && _type != file_type::directory)
        throw std::invalid_argument("\"" + _name.string() +
                                    "\" is not regular file or directory.");
}

std::expected<node_ptr, error_entry> node::find_subnode(unsigned short time_id,
                                                        const path &node_name)
{
    if (!is_single_name(node_name))
        return std::unexpected(error_entry(err_code::not_a_file_name, ""));
    auto subnode_list = subnodes.find(time_id);
    if (subnode_list == subnodes.end())
        return std::unexpected(error_entry(err_code::file_or_dir_unfound, ""));
    for (auto &it : subnode_list->second)
        if (node_name == it->name)
            return it;
    return std::unexpected(error_entry(err_code::file_or_dir_unfound, ""));
}

std::expected<const_node_ptr, error_entry>
node::find_subnode(unsigned short time_id, const path &node_name) const
{
    if (!is_single_name(node_name))
        return std::unexpected(error_entry(err_code::not_a_file_name, ""));
    auto subnode_list = subnodes.find(time_id);
    if (subnode_list == subnodes.end())
        return std::unexpected(error_entry(err_code::file_or_dir_unfound, ""));
    for (auto &it : subnode_list->second)
        if (node_name == it->name)
            return it;
    return std::unexpected(error_entry(err_code::file_or_dir_unfound, ""));
}

std::expected<void, error_entry>
node::touch_subnode(unsigned short time_id,
                    const std::list<node>::iterator &new_node)
{
    if (find_subnode(time_id, new_node->name))
        return std::unexpected(error_entry(err_code::file_or_dir_exists, ""));
    auto subnode_list = subnodes.find(time_id);
    if (subnode_list == subnodes.end())
        subnodes.emplace(time_id,
                         std::vector<std::list<node>::iterator>({new_node}));
    else
        subnode_list->second.push_back(new_node);
    return {};
}

std::expected<std::string_view, error_entry>
node::inquire_checksum(unsigned short time_id) const
{
    auto inquire = checksums.find(time_id);
    if (inquire == checksums.end())
        return std::unexpected(error_entry(err_code::file_or_dir_unfound, ""));
    return static_cast<std::string_view>(inquire->second);
}

void node::print_subnodes(unsigned short time_id, std::ostream &os) const
{
    auto subnode_list = subnodes.find(time_id);
    if (subnode_list == subnodes.end())
        return;
    for (auto &it : subnode_list->second)
        os << it->name << std::endl;
}

// ---------- Timeline Filesystem ----------

timeline_fs::timeline_fs()
{
    node_list.emplace_back("root", file_type::directory);
    node_list.front().parent_node = node_list.begin();
}

node_ptr timeline_fs::root()
{
    return node_list.begin();
}

const_node_ptr timeline_fs::root() const
{
    return node_list.begin();
}

std::expected<unsigned short, error_entry>
timeline_fs::add_time(const std::string &exp)
{
    if (timemapper.size() >= USHRT_MAX)
        return std::unexpected(error_entry(err_code::invalid_operation,
                                           "cannot contain more time id!"));
    for (auto i = randum_time_id();; i = randum_time_id())
    {
        for (auto &it : timemapper)
            if (it.second == i)
                goto regen;
        timemapper.emplace(exp, i);
        return i;
    regen:
    }
}

std::expected<const_node_ptr, error_entry>
timeline_fs::inquire_node(unsigned short time_id, const_node_ptr from,
                          const path &relative_path) const
{
    return walk_path<const_node_ptr>(time_id, from, relative_path);
}

std::expected<node_ptr, error_entry>
timeline_fs::inquire_node_mutable(unsigned short time_id, const node_ptr &from,
                                  const path &relative_path)
{
    return walk_path<node_ptr>(time_id, from, relative_path);
}

std::expected<node_ptr, error_entry>
timeline_fs::touch_node(unsigned short time_id, const node_ptr &ptr,
                        const path &node_name, file_type type)
{
    try
    {
        node_list.emplace_back(node_name, type);
    }
    catch (const std::exception &e)
    {
        return std::unexpected(
            error_entry(err_code::invalid_operation, e.what()));
    }
    auto new_node = std::prev(node_list.end());
    new_node->parent_node = ptr;
    auto touch = ptr->touch_subnode(time_id, new_node);
    if (touch)
        return new_node;
    else
    {
        node_list.erase(new_node);
        return std::unexpected(touch.error());
    }
}

std::expected<node_ptr, error_entry>
timeline_fs::make_dir(unsigned short time_id, const node_ptr &from,
                      const path &relative_path)
{
    node_ptr cursor = from;
    path tmp_p;
    for (auto it = relative_path.begin(); it != relative_path.end(); it++)
    {
        if (it->empty())
            break;
        tmp_p /= *it;
        if (*it == ".")
            continue;
        if (*it == "..")
        {
            cursor = cursor->parent_node;
            continue;
        }
        auto find = cursor->find_subnode(time_id, *it);
        if (find)
        {
            if (find.value()->type != file_type::directory)
                return std::unexpected(
                    error_entry(err_code::not_a_directory,
                                tmp_p.string() + " is not a directory"));
            cursor = find.value();
            continue;
        }
        if (find.error().ec != err_code::file_or_dir_unfound)
            return std::unexpected(find.error());
        auto touch = touch_node(time_id, cursor, *it, file_type::directory);
        if (!touch)
            return std::unexpected(touch.error());
        cursor = touch.value();
    }
    return cursor;
}

std::expected<node_ptr, error_entry>
timeline_fs::make_file(unsigned short time_id, const node_ptr &from,
                       const path &relative_path)
{
    auto mkdir = make_dir(time_id, from, relative_path.parent_path());
    if (!mkdir)
        return std::unexpected(mkdir.error());
    return touch_node(time_id, mkdir.value(), relative_path.filename(),
                      file_type::regular);
}

void timeline_fs::remove_node_internal(unsigned short time_id,
                                       const node_ptr &node_)
{
    auto subnode_list = node_->subnodes.find(time_id);
    if (subnode_list != node_->subnodes.end())
    {
        auto children = std::move(subnode_list->second);
        node_->subnodes.erase(subnode_list);
        for (const auto &child : children)
            remove_node_internal(time_id, child);
    }
    node_->checksums.erase(time_id);

    if (node_->parent_node == node_)
        return;
    for (const auto &entry : node_->parent_node->subnodes)
        for (const auto &sibling : entry.second)
            if (sibling == node_)
                return;
    if (node_->subnodes.empty() && node_->checksums.empty())
        node_list.erase(node_);
}

std::expected<void, error_entry>
timeline_fs::remove_node(unsigned short time_id, const node_ptr &from,
                         const path &relative_path)
{
    if (relative_path.empty())
        return std::unexpected(error_entry(err_code::invalid_operation,
                                           "refuse to remove the pwd."));
    auto target = inquire_node_mutable(time_id, from, relative_path);
    if (!target)
        return std::unexpected(target.error());
    if (target.value() == from || target.value()->parent_node == target.value())
        return std::unexpected(error_entry(err_code::invalid_operation,
                                           "refuse to remove the pwd."));

    auto parent = target.value()->parent_node;
    auto siblings = parent->subnodes.find(time_id);
    if (siblings == parent->subnodes.end())
        return std::unexpected(error_entry(err_code::file_or_dir_unfound, ""));
    for (auto it = siblings->second.begin(); it != siblings->second.end(); ++it)
        if (*it == target.value())
        {
            siblings->second.erase(it);
            remove_node_internal(time_id, target.value());
            return {};
        }
    return std::unexpected(error_entry(err_code::file_or_dir_unfound, ""));
}

std::expected<void, error_entry>
timeline_fs::update_checksum(unsigned short time_id, const node_ptr node_,
                             const std::string &checksum)
{
    if (node_->type != file_type::regular)
        return std::unexpected(error_entry(err_code::not_a_regular, ""));
    auto findtime = node_->checksums.find(time_id);
    if (findtime == node_->checksums.end())
        node_->checksums.emplace(time_id, checksum);
    else
        findtime->second = checksum;
    return {};
}

std::expected<std::string_view, error_entry>
timeline_fs::inquire_checksum(unsigned short time_id, const_node_ptr from,
                              const path &relative_path) const
{
    auto node = inquire_node(time_id, from, relative_path);
    if (!node)
        return std::unexpected(node.error());
    return node.value()->inquire_checksum(time_id);
}

} // namespace kousei