// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_DPFRAMEWORK_HPP
#define LCPROJECTS_TSUBAKI_DPFRAMEWORK_HPP

#include "dynamic_provider_header.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#define TSUBAKI_PROVIDERS_ENV_NAME "TSUBAKI_DYNAMIC_PROVIDER_PATHS"

#define TSUBAKI_SYMBOL_NAME "tsubaki_provider_name"
#define TSUBAKI_SYMBOL_ALGO_INFO "tsubaki_algo_info"
#define TSUBAKI_SYMBOL_IS_KEYED "tsubaki_is_keyed"
#define TSUBAKI_SYMBOL_CHECK_KEY "tsubaki_check_key"
#define TSUBAKI_SYMBOL_IMPLEMENT "tsubaki_checksum_file"

namespace tsubaki
{
const char *no_algo_info() noexcept;

enum class provider_msg
{
    unknown,
    dlopen_fail,
    dlsym_fail,
    refused,
    success,
};

struct provider
{
    provider_msg error = provider_msg::unknown;
    std::filesystem::path display_path;
    void *handle = nullptr;
    const char *(*name)() = nullptr;
    const char *(*algo_info)() = nullptr;
    bool (*is_keyed)() = nullptr;
    bool (*check_key)(const char *) = nullptr;
    tsubaki_checksum_result (*implement)(const char *, const char *) = nullptr;

    provider(const std::filesystem::path &path);
    provider() = default;
};

class dpf_instance // [D]ynamic [P]rovider [F]ramework
{
  private:
    std::vector<std::string> search_paths;
    std::unordered_map<std::string, provider> provider_mapper;

  public:
    const provider not_found;

    dpf_instance();
    dpf_instance(const dpf_instance &) = delete;
    dpf_instance &operator=(const dpf_instance &) = delete;

    const provider &find_provider(const std::string &key) const;
    const std::unordered_map<std::string, provider> &show_mapper() const
    {
        return provider_mapper;
    }
};
} // namespace tsubaki

#endif
