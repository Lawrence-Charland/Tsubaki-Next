// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "dynamic_provider_api.hpp"
#include "basic_taskstream.hpp"
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
    error = provider_msg::success;
}

dpa_instance::dpa_instance()
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

const provider &dpa_instance::find_provider(const std::string &key) const
{
    auto find = provider_mapper.find(key);
    if (find == provider_mapper.end())
        return not_found;
    return find->second;
}
} // namespace tsubaki