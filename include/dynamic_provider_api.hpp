// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_API_HPP
#define LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_API_HPP

#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#define TSUBAKI_PROVIDERS_ENV_NAME "TSUBAKI_DYNAMIC_PROVIDER_PATHS"

#define TSUBAKI_SYMBOL_NAME "tsubaki_provider_name"
#define TSUBAKI_SYMBOL_IS_KEYED "tsubaki_is_keyed"
#define TSUBAKI_SYMBOL_CHECK_KEY "tsubaki_check_key"
#define TSUBAKI_SYMBOL_IMPLEMENT "tsubaki_checksum_file"

namespace tsubaki
{
enum class provider_msg
{
    unknown,
    dlopen_fail,
    dlsym_fail,
    success,
};

struct provider
{
    provider_msg error = provider_msg::unknown;
    std::filesystem::path display_path;
    void *handle = nullptr;
    const char *(*name)() = nullptr;
    bool (*is_keyed)() = nullptr;
    bool (*check_key)(const std::string &) = nullptr;
    void (*implement)(const std::filesystem::path &, const std::string &,
                      std::expected<std::string, std::error_code> &) = nullptr;

    provider(const std::filesystem::path &path);
    provider() = default;
};

class dpa_instance //[D]ynamic [P]rovider [A]pi
{
  private:
    std::vector<std::string> search_paths;
    std::unordered_map<std::string, provider> provider_mapper;

  public:
    const provider not_found;

    dpa_instance();
    dpa_instance(const dpa_instance &) = delete;
    dpa_instance &operator=(const dpa_instance &) = delete;

    const provider &find_provider(const std::string &key) const;
    const std::unordered_map<std::string, provider> &show_mapper() const
    {
        return provider_mapper;
    }
};
} // namespace tsubaki

#endif
