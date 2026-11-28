// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "dpframework.hpp"
#include "basic_taskstream.hpp"
#include "checksum.hpp"
#include "dirscanner.hpp"
#include "utilities.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cctype>
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tsubaki
{
namespace
{
#if defined(_WIN32)
constexpr const char *provider_extension = ".dll";
constexpr char search_path_separator = ';';

void *find_symbol(void *module, const char *name)
{
    return reinterpret_cast<void *>(
        GetProcAddress(static_cast<HMODULE>(module), name));
}
#else
constexpr const char *provider_extension = ".so";
constexpr char search_path_separator = ':';

void *find_symbol(void *module, const char *name)
{
    dlerror();
    void *found = dlsym(module, name);
    return dlerror() ? nullptr : found;
}
#endif

// Windows filesystems are case insensitive; elsewhere the match stays exact.
bool has_provider_extension(const std::filesystem::path &p)
{
#if defined(_WIN32)
    std::string extension = from_fs_path(p.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return extension == provider_extension;
#else
    return p.extension() == provider_extension;
#endif
}
} // namespace

const char *no_algo_info() noexcept
{
    return "No information provided.";
}

bool dont_need_args() noexcept
{
    return false;
}

bool not_keyed() noexcept
{
    return false;
}

bool fake_check_key(const char *, std::size_t) noexcept
{
    return true;
}

provider::provider(const std::filesystem::path &path) : display_path(path)
{
#if defined(_WIN32)
    handle = LoadLibraryW(display_path.wstring().c_str());
#else
    handle = dlopen(display_path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!handle)
    {
        error = provider_msg::dlopen_fail;
        return;
    }
    name = reinterpret_cast<decltype(name)>(
        find_symbol(handle, TSUBAKI_SYMBOL_NAME));
    if (!name)
    { // missing name is unacceptable
        error = provider_msg::dlsym_fail;
        return;
    }
    if (str_to_alg(name()) != algorithm::error)
    { // shadowing a built-in name is unacceptable
        error = provider_msg::refused;
        return;
    }
    algo_info = reinterpret_cast<decltype(algo_info)>(
        find_symbol(handle, TSUBAKI_SYMBOL_ALGO_INFO));
    if (!algo_info)
        // no info is acceptable
        algo_info = no_algo_info;
    is_keyed = reinterpret_cast<decltype(is_keyed)>(
        find_symbol(handle, TSUBAKI_SYMBOL_IS_KEYED));
    if (!is_keyed)
        // default to unkeyed
        goto no_key;
    if (is_keyed() == false)
    {
    no_key:
        is_keyed = not_keyed;
        check_key = fake_check_key;
    }
    else
    {
        check_key = reinterpret_cast<decltype(check_key)>(
            find_symbol(handle, TSUBAKI_SYMBOL_CHECK_KEY));
        if (!check_key)
        { // missing ckecker is unacceptable
            error = provider_msg::dlsym_fail;
            return;
        }
    }
    implement = reinterpret_cast<decltype(implement)>(
        find_symbol(handle, TSUBAKI_SYMBOL_IMPLEMENT));
    if (!implement)
    { // no implement
        error = provider_msg::dlsym_fail;
        return;
    }
    need_args = reinterpret_cast<decltype(need_args)>(
        find_symbol(handle, TSUBAKI_SYMBOL_NEED_ARGS));
    if (!need_args)
        // default to no args
        goto no_args;
    if (need_args() == false)
    {
    no_args:
        error = provider_msg::success;
        need_args = dont_need_args;
        return;
    }
    arg_def = reinterpret_cast<decltype(arg_def)>(
        find_symbol(handle, TSUBAKI_SYMBOL_ARGDEF));
    if (!arg_def)
    {
        error = provider_msg::dlsym_fail;
        return;
    }
    take_args = reinterpret_cast<decltype(take_args)>(
        find_symbol(handle, TSUBAKI_SYMBOL_TAKE_ARGS));
    if (!take_args)
    {
        error = provider_msg::dlsym_fail;
        return;
    }
    error = provider_msg::success;
}

dpf_instance::dpf_instance()
{
    search_paths =
        get_env_arr(TSUBAKI_PROVIDERS_ENV_NAME, search_path_separator);
    for (auto &path : search_paths)
    {
        try
        {
            dirscanner so_scanner(to_fs_path(path), {}, false);
            std::optional<task_token> target;
            while ((target = so_scanner.pop(), target))
            {
                // Only provider modules can be loaded; the directory may
                // hold anything else.
                if (!has_provider_extension(target.value().p))
                    continue;
                provider so_open(target.value().p);
                const std::string shown =
                    from_fs_path(target.value().p);
                switch (so_open.error)
                {
                case provider_msg::success:
                    provider_mapper.emplace(so_open.name(), std::move(so_open));
                    break;
                case provider_msg::dlopen_fail:
                    std::cerr << "Error: dlopen failed for \"" << shown
                              << "\"" << std::endl;
                    break;
                case provider_msg::dlsym_fail:
                    std::cerr << "Error: dlsym failed for \"" << shown
                              << "\"" << std::endl;
                    break;
                case provider_msg::refused:
                    std::cerr << "Error: refused to load \"" << shown << "\""
                              << std::endl;
                    break;
                default:
                    std::cerr << "Error: failed to load provider \"" << shown
                              << "\"." << std::endl;
                    break;
                }
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
}

const provider &dpf_instance::find_provider(const std::string &key) const
{
    auto find = provider_mapper.find(key);
    if (find == provider_mapper.end())
        return not_found;
    return find->second;
}
} // namespace tsubaki