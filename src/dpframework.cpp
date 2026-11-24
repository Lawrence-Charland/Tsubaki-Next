// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "dpframework.hpp"
#include "basic_taskstream.hpp"
#include "checksum.hpp"
#include "dirscanner.hpp"
#include "utilities.hpp"

#include <dlfcn.h>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace tsubaki
{
const char *no_algo_info() noexcept
{
    return "No information provided.";
}

bool dont_need_args() noexcept
{
    return false;
}

provider::provider(const std::filesystem::path &path) : display_path(path)
{
    handle = dlopen(display_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        error = provider_msg::dlopen_fail;
        return;
    }
    dlerror();
    name = reinterpret_cast<decltype(name)>(dlsym(handle, TSUBAKI_SYMBOL_NAME));
    if (const char *err = dlerror())
    {
        error = provider_msg::dlsym_fail;
        return;
    }
    if (str_to_alg(name()) != algorithm::error)
    {
        error = provider_msg::refused;
        return;
    }
    algo_info = reinterpret_cast<decltype(algo_info)>(
        dlsym(handle, TSUBAKI_SYMBOL_ALGO_INFO));
    if (const char *err = dlerror())
        algo_info = no_algo_info;
    is_keyed = reinterpret_cast<decltype(is_keyed)>(
        dlsym(handle, TSUBAKI_SYMBOL_IS_KEYED));
    if (const char *err = dlerror())
    {
        error = provider_msg::dlsym_fail;
        return;
    }
    check_key = reinterpret_cast<decltype(check_key)>(
        dlsym(handle, TSUBAKI_SYMBOL_CHECK_KEY));
    if (const char *err = dlerror())
    {
        error = provider_msg::dlsym_fail;
        return;
    }
    implement = reinterpret_cast<decltype(implement)>(
        dlsym(handle, TSUBAKI_SYMBOL_IMPLEMENT));
    if (const char *err = dlerror())
    {
        error = provider_msg::dlsym_fail;
        return;
    }
    need_args = reinterpret_cast<decltype(need_args)>(
        dlsym(handle, TSUBAKI_SYMBOL_NEED_ARGS));
    if (const char *err = dlerror())
    {
        error = provider_msg::success;
        need_args = dont_need_args;
        return;
    }
    arg_def = reinterpret_cast<decltype(arg_def)>(
        dlsym(handle, TSUBAKI_SYMBOL_ARGDEF));
    if (const char *err = dlerror())
    {
        error = provider_msg::dlsym_fail;
        return;
    }
    take_args = reinterpret_cast<decltype(take_args)>(
        dlsym(handle, TSUBAKI_SYMBOL_TAKE_ARGS));
    if (const char *err = dlerror())
    {
        error = provider_msg::dlsym_fail;
        return;
    }
    error = provider_msg::success;
}

dpf_instance::dpf_instance()
{
    search_paths = get_env_arr(TSUBAKI_PROVIDERS_ENV_NAME, ':');
    for (auto &path : search_paths)
    {
        try
        {
            dirscanner so_scanner(std::filesystem::path(path), {}, false);
            std::optional<task_token> target;
            while ((target = so_scanner.pop(), target))
            {
                // Only shared objects can be providers; the directory may hold
                // anything else.
                if (target.value().p.extension() != ".so")
                    continue;
                provider so_open(target.value().p);
                switch (so_open.error)
                {
                case provider_msg::success:
                    provider_mapper.emplace(so_open.name(), std::move(so_open));
                    break;
                case provider_msg::dlopen_fail:
                    std::cerr << "Error: dlopen failed for \""
                              << target.value().p.string() << "\"" << std::endl;
                    break;
                case provider_msg::dlsym_fail:
                    std::cerr << "Error: dlsym failed for \""
                              << target.value().p.string() << "\"" << std::endl;
                    break;
                case provider_msg::refused:
                    std::cerr << "Error: refused to load \""
                              << target.value().p.string() << "\"" << std::endl;
                    break;
                default:
                    std::cerr << "Error: failed to load provider \""
                              << target.value().p.string() << "\"."
                              << std::endl;
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